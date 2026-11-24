#include "screen_capturer_dxgi.h"

#include <display_stream_id.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "captured_cursor_state.h"
#include "cursor_frame_compositor.h"
#include "display_label.h"
#include "dxgi_cursor_state.h"
#include "dxgi_nv12_frames.h"
#include "libyuv.h"
#include "rd_log.h"
#include "windows_thread_dpi.h"
#include "../virtual_display/usbmmidd_virtual_display.h"

namespace crossdesk {

namespace {
// Keep static streams available to joining viewers and inside MiniRTC's
// 100 ms active-stream window. Actual presents and cursor changes bypass this.
constexpr auto kStaticRefreshInterval = std::chrono::milliseconds(65);

std::wstring GetOutputDeviceId(const std::wstring& device_name) {
  DISPLAY_DEVICEW device{sizeof(device)};
  if (!EnumDisplayDevicesW(device_name.c_str(), 0, &device,
                           EDD_GET_DEVICE_INTERFACE_NAME)) {
    return {};
  }
  return device.DeviceID;
}
}  // namespace

struct ScreenCapturerDxgi::FrameCursor {
  CURSORINFO info{sizeof(CURSORINFO)};
  bool draw = false;
  bool already_embedded = false;

  FrameCursor() = default;
  FrameCursor(const DisplayInfo& display, bool show_cursor) {
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING)) return;
    const bool separate =
        SharedDxgiCursorState().ShouldDrawCursor(true, display.handle);
    const RECT bounds{display.left, display.top, display.right, display.bottom};
    draw = show_cursor && separate &&
           (PtInRect(&bounds, info.ptScreenPos) ||
            CursorIntersectsCapture(info, display.left, display.top, display.width,
                                    display.height));
    already_embedded = !separate &&
        MonitorFromPoint(info.ptScreenPos, MONITOR_DEFAULTTONULL) == display.handle;
  }

  bool SameImageAs(const FrameCursor& other) const {
    return already_embedded == other.already_embedded && draw == other.draw &&
           (!draw || (info.hCursor == other.info.hCursor &&
                      info.ptScreenPos.x == other.info.ptScreenPos.x &&
                      info.ptScreenPos.y == other.info.ptScreenPos.y));
  }
};

ScreenCapturerDxgi::ScreenCapturerDxgi() {}
ScreenCapturerDxgi::~ScreenCapturerDxgi() {
  Destroy();
}

int ScreenCapturerDxgi::Init(const int fps, cb_desktop_data cb) {
  fps_ = fps;
  callback_ = cb;
  if (!callback_) {
    LOG_ERROR("DXGI: callback is null");
    return -1;
  }

  display_slots_.Reset();
  display_info_list_.clear();
  topology_refresh_requested_.store(false);
  EnumerateDisplays();
  if (display_info_list_.empty()) {
    LOG_ERROR("DXGI: no displays found");
    return -3;
  }

  monitor_index_ = 0;
  return 0;
}

int ScreenCapturerDxgi::Destroy() {
  Stop();
  ReleaseDuplication();
  outputs_.clear();
  display_info_list_.clear();
  display_slots_.Reset();
  d3d_context_.Reset();
  d3d_device_.Reset();
  if (nv12_frame_) {
    delete[] nv12_frame_;
    nv12_frame_ = nullptr;
    nv12_width_ = 0;
    nv12_height_ = 0;
  }
  return 0;
}

int ScreenCapturerDxgi::Start(bool show_cursor) {
  if (running_) return 0;
  show_cursor_ = show_cursor;

  if (!CreateDuplicationForMonitor(monitor_index_)) {
    LOG_ERROR("DXGI: create duplication failed for monitor {}",
              monitor_index_.load());
    return -1;
  }

  paused_ = false;
  running_ = true;
  thread_ = std::thread([this]() { CaptureLoop(); });
  return 0;
}

int ScreenCapturerDxgi::Stop() {
  if (!running_) return 0;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  ReleaseDuplication();
  return 0;
}

int ScreenCapturerDxgi::Pause(int monitor_index) {
  paused_ = true;
  return 0;
}

int ScreenCapturerDxgi::Resume(int monitor_index) {
  paused_ = false;
  return 0;
}

int ScreenCapturerDxgi::SwitchTo(int monitor_index) {
  std::lock_guard<std::mutex> lock(switch_mutex_);
  if (monitor_index < 0 || monitor_index >= (int)display_info_list_.size()) {
    LOG_ERROR("DXGI: invalid monitor index {}", monitor_index);
    return -1;
  }
  if (monitor_index == monitor_index_ && (!running_ || duplication_)) return 0;
  // Select before Start without allocating a second D3D device/duplication.
  if (!running_) {
    monitor_index_ = monitor_index;
    return 0;
  }
  paused_ = true;
  const int previous = monitor_index_.exchange(monitor_index);
  ReleaseDuplication();
  if (!CreateDuplicationForMonitor(monitor_index)) {
    LOG_ERROR("DXGI: create duplication failed for monitor {}", monitor_index);
    // Keep the backend on the output it was capturing so the wrapper's index
    // and ours do not diverge after a rejected switch.
    monitor_index_ = previous;
    if (!CreateDuplicationForMonitor(previous)) {
      LOG_WARN("DXGI: could not restore duplication for monitor {}", previous);
    }
    paused_ = false;
    return -2;
  }
  paused_ = false;
  LOG_INFO("DXGI: switched to monitor {}:{}", monitor_index_.load(),
           display_info_list_[monitor_index_].name);
  return 0;
}

int ScreenCapturerDxgi::ResetToInitialMonitor() {
  return SwitchTo(0);
}

bool ScreenCapturerDxgi::EnumerateDisplays() {
  ScopedWindowsPhysicalCoordinates physical_coordinates;
  // Factories cache adapter topology. Refresh the factory as well as outputs.
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  const HRESULT factory_result =
      CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(factory_result)) {
    LOG_ERROR("DXGI: CreateDXGIFactory1 failed, hr={}", (int)factory_result);
    return false;
  }
  struct Output {
    DisplayInfo display;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    std::string identity;
  };
  std::vector<Output> current;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  for (UINT a = 0;
       factory->EnumAdapters(a, adapter.ReleaseAndGetAddressOf()) !=
       DXGI_ERROR_NOT_FOUND; ++a) {
    if (!adapter) break;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) !=
                       DXGI_ERROR_NOT_FOUND; ++o) {
      if (!output) break;
      DXGI_OUTPUT_DESC desc{};
      if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) continue;
      MONITORINFOEX mi{};
      mi.cbSize = sizeof(mi);
      if (!GetMonitorInfo(desc.Monitor, &mi)) continue;
      const auto device_id = GetOutputDeviceId(desc.DeviceName);
      current.push_back({
          DisplayInfo(desc.Monitor, GetDisplayLabel(desc.DeviceName),
                       (mi.dwFlags & MONITORINFOF_PRIMARY) != 0,
                       mi.rcMonitor.left, mi.rcMonitor.top,
                       mi.rcMonitor.right, mi.rcMonitor.bottom),
          output, WideToUtf8(device_id.empty() ? desc.DeviceName : device_id)});
    }
  }
  std::stable_partition(current.begin(), current.end(),
                         [](const auto& item) { return item.display.is_primary; });
  std::vector<std::string> identities;
  for (const auto& item : current) identities.push_back(item.identity);
  const auto slots = display_slots_.Update(identities);
  std::vector<DisplayInfo> displays;
  std::vector<Microsoft::WRL::ComPtr<IDXGIOutput>> outputs;
  bool changed = slots.size() != display_info_list_.size();
  for (size_t slot = 0; slot < slots.size(); ++slot) {
    if (slots[slot] < 0) {
      auto missing = display_info_list_[slot];
      missing.handle = nullptr;
      missing.left = missing.top = missing.right = missing.bottom = 0;
      missing.width = missing.height = 0;
      missing.is_primary = false;
      displays.push_back(std::move(missing));
      outputs.emplace_back();
    } else {
      const auto& item = current[slots[slot]];
      displays.push_back(item.display);
      outputs.push_back(item.output);
    }
    if (slot >= display_info_list_.size()) continue;
    const auto& before = display_info_list_[slot];
    const auto& after = displays.back();
    changed = changed || before.handle != after.handle ||
              before.left != after.left || before.top != after.top ||
              before.right != after.right || before.bottom != after.bottom ||
              before.is_primary != after.is_primary || before.name != after.name;
  }
  display_info_list_ = std::move(displays);
  outputs_ = std::move(outputs);
  if (changed) {
    for (size_t slot = 0; slot < display_info_list_.size(); ++slot) {
      const auto& display = display_info_list_[slot];
      LOG_INFO("DXGI: session display slot={} name='{}' available={} "
               "bounds=({},{};{},{}), selected={}",
               slot, display.name, outputs_[slot] != nullptr,
               display.left, display.top, display.right, display.bottom,
               static_cast<int>(slot) == monitor_index_.load());
    }
  }
  return changed;
}

bool ScreenCapturerDxgi::CreateDuplicationForMonitor(int monitor_index) {
  SharedDxgiCursorState().Reset();
  if (monitor_index < 0 || monitor_index >= (int)outputs_.size() ||
      !outputs_[monitor_index]) return false;

  // Outputs may move between GPU adapters when the display topology changes.
  // Desktop Duplication requires the D3D device to be created from the same
  // adapter as the selected IDXGIOutput.
  Microsoft::WRL::ComPtr<IDXGIAdapter> output_adapter;
  HRESULT hr = outputs_[monitor_index]->GetParent(
      IID_PPV_ARGS(output_adapter.GetAddressOf()));
  if (FAILED(hr) || !output_adapter) {
    LOG_ERROR("DXGI: get adapter for output {} failed, hr={}", monitor_index,
              (int)hr);
    return false;
  }

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL out_level{};
  Microsoft::WRL::ComPtr<ID3D11Device> output_device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> output_context;
  hr = D3D11CreateDevice(
      output_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
      feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION,
      output_device.GetAddressOf(), &out_level, output_context.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("DXGI: create D3D device for output adapter {} failed, hr={}",
              monitor_index, (int)hr);
    return false;
  }
  d3d_context_ = std::move(output_context);
  d3d_device_ = std::move(output_device);

  Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
  hr = outputs_[monitor_index]->QueryInterface(
      IID_PPV_ARGS(output1.GetAddressOf()));
  if (FAILED(hr)) {
    LOG_ERROR("DXGI: Query IDXGIOutput1 failed, hr={}", (int)hr);
    return false;
  }

  duplication_.Reset();
  hr = output1->DuplicateOutput(d3d_device_.Get(), duplication_.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("DXGI: DuplicateOutput failed, hr={}", (int)hr);
    return false;
  }

  staging_.Reset();
  DXGI_OUTDUPL_DESC desc{};
  duplication_->GetDesc(&desc);
  rotation_ = desc.Rotation;
#if USE_CUDA
  if (rotation_ == DXGI_MODE_ROTATION_IDENTITY || rotation_ == DXGI_MODE_ROTATION_UNSPECIFIED) {
    DXGI_ADAPTER_DESC adapter{};
    if (SUCCEEDED(output_adapter->GetDesc(&adapter)) && adapter.VendorId == 0x10DE)
      gpu_frames_ = std::make_unique<DxgiNv12Frames>(d3d_device_.Get());
  }
#endif
  DXGI_ADAPTER_DESC adapter_desc{};
  output_adapter->GetDesc(&adapter_desc);
  DXGI_OUTPUT_DESC output_desc{};
  software_cursor_output_ =
      SUCCEEDED(outputs_[monitor_index]->GetDesc(&output_desc)) &&
      IsUsbmmiddDisplayDevice(output_desc.DeviceName);
  SharedDxgiCursorState().BeginOutput(
      display_info_list_[monitor_index].handle, software_cursor_output_);
  last_logged_cursor_embedding_ = -1;
  LOG_INFO("DXGI: duplication ready, monitor={}, rotation={}, adapter='{}'",
           monitor_index, static_cast<int>(rotation_),
           WideToUtf8(adapter_desc.Description));
  return true;
}

void ScreenCapturerDxgi::ReleaseDuplication() {
  // A stopped candidate may be destroyed after its replacement has started.
  // Only the live duplication owns the shared pointer-plane state.
  if (duplication_) SharedDxgiCursorState().Reset();
  ++duplication_generation_;
  gpu_frames_.reset();
  staging_.Reset();
  ReleaseAcquiredFrame();
  duplication_.Reset();
}

HRESULT ScreenCapturerDxgi::ReleaseAcquiredFrame() {
  if (!frame_acquired_) return S_OK;
  frame_acquired_ = false;
  const HRESULT result = duplication_->ReleaseFrame();
  if (FAILED(result))
    LOG_WARN("DXGI: ReleaseFrame failed, hr=0x{:08X}",
             static_cast<uint32_t>(result));
  return result;
}

bool ScreenCapturerDxgi::StageTexture(ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc{}, existing{};
  texture->GetDesc(&desc);
  if (staging_) staging_->GetDesc(&existing);
  if (!staging_ || existing.Width != desc.Width || existing.Height != desc.Height ||
      existing.Format != desc.Format) {
    staging_.Reset();
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d3d_device_->CreateTexture2D(&desc, nullptr, &staging_))) return false;
  }
  d3d_context_->CopyResource(staging_.Get(), texture);
  return true;
}

bool ScreenCapturerDxgi::ConvertFrame(int frame_monitor, const FrameCursor& cursor,
                                      bool* cursor_embedded,
                                      CursorFrameCompositor& compositor) {
  *cursor_embedded = false;
  if (!staging_) return false;
  D3D11_TEXTURE2D_DESC src_desc{};
  staging_->GetDesc(&src_desc);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT hr =
      d3d_context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) {
    return false;
  }

  auto pixels = static_cast<const uint8_t*>(mapped.pData);
  int stride = static_cast<int>(mapped.RowPitch);
  int logical_width = static_cast<int>(src_desc.Width);
  int logical_height = static_cast<int>(src_desc.Height);
  libyuv::RotationMode rotation = libyuv::kRotate0;
  switch (rotation_) {
    case DXGI_MODE_ROTATION_ROTATE90:
      rotation = libyuv::kRotate90;
      break;
    case DXGI_MODE_ROTATION_ROTATE180:
      rotation = libyuv::kRotate180;
      break;
    case DXGI_MODE_ROTATION_ROTATE270:
      rotation = libyuv::kRotate270;
      break;
    default:
      break;
  }
  if (rotation != libyuv::kRotate0) {
    const bool swap_axes = rotation != libyuv::kRotate180;
    const int rotated_width = swap_axes ? logical_height : logical_width;
    rotated_frame_.resize(static_cast<size_t>(logical_width) * logical_height *
                          4);
    if (libyuv::ARGBRotate(pixels, stride, rotated_frame_.data(),
                           rotated_width * 4, logical_width, logical_height,
                           rotation) != 0) {
      d3d_context_->Unmap(staging_.Get(), 0);
      return false;
    }
    pixels = rotated_frame_.data();
    stride = rotated_width * 4;
    if (swap_axes) std::swap(logical_width, logical_height);
  }
  int even_width = logical_width & ~1;
  int even_height = logical_height & ~1;
  if (even_width <= 0 || even_height <= 0) {
    d3d_context_->Unmap(staging_.Get(), 0);
    return false;
  }

  *cursor_embedded = cursor.already_embedded;
  if (cursor.draw) {
    const auto& display = display_info_list_[frame_monitor];
    if (const auto* composited =
            compositor.Draw(pixels, stride, even_width, even_height,
                            cursor.info, display.left, display.top)) {
      pixels = composited;
      stride = even_width * 4;
      *cursor_embedded = true;
    }
  }

  int nv12_size = even_width * even_height * 3 / 2;
  if (!nv12_frame_ || nv12_width_ != even_width ||
      nv12_height_ != even_height) {
    delete[] nv12_frame_;
    nv12_frame_ = new unsigned char[nv12_size];
    nv12_width_ = even_width;
    nv12_height_ = even_height;
  }

  const int converted =
      libyuv::ARGBToNV12(pixels, stride, nv12_frame_, even_width,
                         nv12_frame_ + even_width * even_height, even_width,
                         even_width, even_height);

  d3d_context_->Unmap(staging_.Get(), 0);
  return converted == 0;
}

void ScreenCapturerDxgi::CaptureLoop() {
  ScopedWindowsPhysicalCoordinates physical_coordinates;
  CursorFrameCompositor cursor_compositor;
  const int timeout_ms = (std::max)(1, 1000 / (std::max)(1, fps_));
  bool cached_frame_valid = false;
  bool cached_cursor_embedded = false;
  FrameCursor cached_cursor;
  Microsoft::WRL::ComPtr<DxgiNv12Frame> cached_gpu;
  const auto deliver = [&](const std::string& stream_id, bool embedded) {
    CapturedCursorFrameScope cursor_scope(embedded);
    if (cached_gpu) {
      const auto* frame = cached_gpu->Descriptor();
      callback_(nullptr, frame->width * frame->height * 3 / 2, frame->width,
                frame->height, stream_id.c_str(), frame);
    } else {
      callback_(nv12_frame_, nv12_width_ * nv12_height_ * 3 / 2, nv12_width_,
                nv12_height_, stream_id.c_str(), nullptr);
    }
  };
  std::chrono::steady_clock::time_point last_delivery{};
  int cached_monitor = -1;
  uint64_t cached_generation = 0;
  auto next_topology_check = std::chrono::steady_clock::now();
  unsigned acquire_failures = 0;
  auto cursor_revision = SharedDxgiCursorState().CursorVisibilityRevision();
  while (running_) {
    if (paused_) {
      cached_gpu.Reset();
      cached_frame_valid = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Duplication/staging textures belong to one monitor. Do not release or
    // replace them in SwitchTo while AcquireNextFrame/Map is using them.
    std::unique_lock capture_lock(switch_mutex_);
    if (paused_ || !running_) continue;
    if (cached_generation != duplication_generation_) {
      // A switched or lost output may never deliver another present.
      cached_gpu.Reset();
      cached_frame_valid = false;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto revision = SharedDxgiCursorState().CursorVisibilityRevision();
    // An IDD can bake the pointer into the last desktop texture. Retire that
    // cached texture when the user helper hides/restores the system cursor;
    // pointer-only updates must not keep replaying its old image.
    const bool refresh = topology_refresh_requested_.exchange(false) ||
                         revision != cursor_revision;
    cursor_revision = revision;
    if (refresh || now >= next_topology_check) {
      next_topology_check = now + std::chrono::milliseconds(500);
      const bool changed = EnumerateDisplays();
      if (changed || refresh || !duplication_) {
        cached_frame_valid = false;
        ReleaseDuplication();
        CreateDuplicationForMonitor(monitor_index_.load());
        capture_lock.unlock();
        if (callback_)
          callback_(nullptr, ScreenCapturer::kDisplayTopologyChanged,
                    0, 0, "", nullptr);
        continue;
      }
    }
    if (!duplication_) {
      cached_frame_valid = false;
      capture_lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    const int frame_monitor = monitor_index_.load();
    const auto read_cursor = [&] {
      return FrameCursor(display_info_list_[frame_monitor],
                         show_cursor_.load(std::memory_order_relaxed));
    };
    const auto deliver_cached = [&](bool pointer_shape_changed) {
      if (!cached_frame_valid || cached_generation != duplication_generation_ ||
          cached_monitor != monitor_index_.load() || !callback_)
        return;
      const auto cursor = read_cursor();
      const bool redraw = !cursor.SameImageAs(cached_cursor) ||
                          (cursor.draw && pointer_shape_changed);
      const auto delivery_time = std::chrono::steady_clock::now();
      if (!redraw && delivery_time - last_delivery < kStaticRefreshInterval)
        return;
      if (cached_gpu && redraw && cursor.draw) {
        // Cursor composition must start from the clean BGRA desktop, even on
        // a pointer-only acquire. The next desktop present can use NV12 again.
        if (!StageTexture(gpu_frames_->CleanDesktop())) return;
        cached_gpu.Reset();
      }
      if (cached_gpu) cached_cursor_embedded = cursor.already_embedded;
      // Retry a failed GDI cursor draw on the next refresh.
      if (!cached_gpu && (redraw || (cursor.draw && !cached_cursor_embedded)) &&
          !ConvertFrame(cached_monitor, cursor, &cached_cursor_embedded,
                        cursor_compositor)) {
        cached_frame_valid = false;
        return;
      }
      cached_cursor = cursor;
      last_delivery = delivery_time;
      const auto stream_id = MakeDisplayStreamId(cached_monitor);
      capture_lock.unlock();
      deliver(stream_id, cached_cursor_embedded);
    };
    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktop_resource;
    HRESULT hr = duplication_->AcquireNextFrame(
        timeout_ms, &frame_info, desktop_resource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
      // DXGI explicitly reports an unchanged desktop. Re-emit only a frame
      // successfully acquired for this monitor to keep delivering static frames.
      deliver_cached(false);
      continue;
    }
    // Never replay a cached image after an acquisition/conversion failure or
    // a backend rebuild. Display recovery retains the session privacy state.
    if (FAILED(hr)) {
      cached_frame_valid = false;
      LOG_ERROR("DXGI: AcquireNextFrame failed, hr={}", (int)hr);
      ReleaseDuplication();
      // A single ACCESS_LOST (mode switch, fullscreen toggle, UAC return) is
      // rebuilt on the next iteration. Only repeated failures fall back to
      // the 500 ms topology cadence so a broken output does not spin while
      // recreating D3D devices.
      if (++acquire_failures == 1) {
        next_topology_check = std::chrono::steady_clock::now();
      }
      continue;
    }
    frame_acquired_ = true;
    acquire_failures = 0;

    if (frame_monitor >= 0 &&
        frame_monitor < static_cast<int>(display_info_list_.size())) {
      SharedDxgiCursorState().Update(
          frame_info.LastMouseUpdateTime.QuadPart,
          frame_info.PointerPosition.Visible != FALSE,
          display_info_list_[frame_monitor].handle);
    }

    // A successful acquire can contain only a cursor update. It is not a new
    // desktop image; in particular, do not seed a newly rebuilt capture with
    // its surface. Keep the last confirmed desktop until a present arrives.
    if (frame_info.LastPresentTime.QuadPart == 0) {
      if (FAILED(ReleaseAcquiredFrame())) {
        cached_frame_valid = false;
        ReleaseDuplication();
        next_topology_check = std::chrono::steady_clock::now();
      } else {
        deliver_cached(frame_info.PointerShapeBufferSize != 0);
      }
      continue;
    }
    cached_frame_valid = false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> acquired_tex;
    if (desktop_resource) {
      hr = desktop_resource->QueryInterface(
          IID_PPV_ARGS(acquired_tex.GetAddressOf()));
      if (FAILED(hr)) {
        ReleaseAcquiredFrame();
        continue;
      }
    } else {
      ReleaseAcquiredFrame();
      continue;
    }

    const auto cursor = read_cursor();
    cached_gpu.Reset();
    if (!cursor.draw && gpu_frames_) cached_gpu = gpu_frames_->Convert(acquired_tex.Get());
    bool cursor_embedded = cursor.already_embedded;
    const bool converted = cached_gpu ||
        (StageTexture(acquired_tex.Get()) &&
         ConvertFrame(frame_monitor, cursor, &cursor_embedded, cursor_compositor));

    const std::string stream_id = MakeDisplayStreamId(frame_monitor);
    cached_generation = duplication_generation_;
    if (converted && frame_monitor == monitor_index_.load() && callback_) {
      if (last_logged_cursor_embedding_ < 0 ||
          (software_cursor_output_ &&
           last_logged_cursor_embedding_ != static_cast<int>(cursor_embedded))) {
        LOG_DEBUG("DXGI cursor: monitor={} usbmmidd={} mouse_update={} "
                 "separate_pointer_visible={} pointer_shape_bytes={} "
                 "compose_cursor={} embedded={}",
                 frame_monitor, software_cursor_output_,
                 frame_info.LastMouseUpdateTime.QuadPart,
                 frame_info.PointerPosition.Visible != FALSE,
                 frame_info.PointerShapeBufferSize,
                 show_cursor_.load(std::memory_order_relaxed), cursor_embedded);
        last_logged_cursor_embedding_ = static_cast<int>(cursor_embedded);
      }
    }
    // A mode/desktop transition can invalidate the acquire during readback.
    // Do not publish that frame if releasing it reports the lost capture.
    if (FAILED(ReleaseAcquiredFrame())) {
      ReleaseDuplication();
      next_topology_check = std::chrono::steady_clock::now();
      continue;
    }
    capture_lock.unlock();

    if (converted && frame_monitor == monitor_index_.load() && callback_) {
      cached_cursor = cursor;
      last_delivery = std::chrono::steady_clock::now();
      deliver(stream_id, cursor_embedded);
      cached_cursor_embedded = cursor_embedded;
      cached_monitor = frame_monitor;
      cached_frame_valid = true;
    }
  }
}

}  // namespace crossdesk
