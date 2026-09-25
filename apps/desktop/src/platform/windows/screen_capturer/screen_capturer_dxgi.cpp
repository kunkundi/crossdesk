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
#include "libyuv.h"
#include "rd_log.h"
#include "windows_thread_dpi.h"
#include "../virtual_display/usbmmidd_virtual_display.h"

namespace crossdesk {

namespace {
std::wstring GetOutputDeviceId(const std::wstring& device_name) {
  DISPLAY_DEVICEW device{sizeof(device)};
  if (!EnumDisplayDevicesW(device_name.c_str(), 0, &device,
                           EDD_GET_DEVICE_INTERFACE_NAME)) {
    return {};
  }
  return device.DeviceID;
}
}  // namespace

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
  SharedDxgiCursorState().Reset();
  ++duplication_generation_;
  staging_.Reset();
  if (duplication_) {
    duplication_->ReleaseFrame();
  }
  duplication_.Reset();
}

bool ScreenCapturerDxgi::ConvertFrame(int frame_monitor, bool* cursor_embedded,
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

  if (frame_monitor >= 0 &&
      frame_monitor < static_cast<int>(display_info_list_.size())) {
    const auto& display = display_info_list_[frame_monitor];
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (GetCursorInfo(&cursor)) {
      const bool visible = (cursor.flags & CURSOR_SHOWING) != 0;
      const bool should_draw = SharedDxgiCursorState().ShouldDrawCursor(
          visible, display.handle);
      // show_cursor controls our extra composition, not cursors the OS has
      // already put in the texture. Publish that distinction for native
      // controllers as well, so they do not draw a second cursor.
      *cursor_embedded = visible && !should_draw &&
          MonitorFromPoint(cursor.ptScreenPos, MONITOR_DEFAULTTONULL) ==
              display.handle;
      if (show_cursor_.load(std::memory_order_relaxed) && should_draw) {
        if (const auto* composited =
                compositor.Draw(pixels, stride, even_width, even_height, cursor,
                                display.left, display.top)) {
          pixels = composited;
          stride = even_width * 4;
          *cursor_embedded = true;
        }
      }
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
  int cached_monitor = -1;
  uint64_t cached_generation = 0;
  auto next_topology_check = std::chrono::steady_clock::now();
  unsigned acquire_failures = 0;
  while (running_) {
    if (paused_) {
      cached_frame_valid = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Duplication/staging textures belong to one monitor. Do not release or
    // replace them in SwitchTo while AcquireNextFrame/Map is using them.
    std::unique_lock capture_lock(switch_mutex_);
    if (paused_ || !running_) continue;
    const auto now = std::chrono::steady_clock::now();
    const bool refresh = topology_refresh_requested_.exchange(false);
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
    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktop_resource;
    HRESULT hr = duplication_->AcquireNextFrame(
        timeout_ms, &frame_info, desktop_resource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
      // DXGI explicitly reports an unchanged desktop. Re-emit only a frame
      // successfully acquired for this monitor to keep delivering static frames.
      if (cached_frame_valid && cached_generation == duplication_generation_ &&
          cached_monitor == monitor_index_.load() && callback_ && nv12_frame_) {
        // Recompose from the clean staging texture while showing a cursor, or
        // once after disabling it. Never replay a baked-in stale cursor.
        if (show_cursor_.load(std::memory_order_relaxed) ||
            cached_cursor_embedded) {
          if (!ConvertFrame(cached_monitor, &cached_cursor_embedded,
                            cursor_compositor)) {
            cached_frame_valid = false;
            continue;
          }
        }
        const auto stream_id = MakeDisplayStreamId(cached_monitor);
        capture_lock.unlock();
        CapturedCursorFrameScope cursor_scope(cached_cursor_embedded);
        callback_(nv12_frame_, nv12_width_ * nv12_height_ * 3 / 2, nv12_width_,
                  nv12_height_, stream_id.c_str(), nullptr);
      }
      continue;
    }
    // Never replay a cached image after an acquisition/conversion failure or
    // a backend rebuild. Display recovery retains the session privacy state.
    cached_frame_valid = false;
    if (FAILED(hr)) {
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
    acquire_failures = 0;

    if (frame_monitor >= 0 &&
        frame_monitor < static_cast<int>(display_info_list_.size())) {
      SharedDxgiCursorState().Update(
          frame_info.LastMouseUpdateTime.QuadPart,
          frame_info.PointerPosition.Visible != FALSE,
          display_info_list_[frame_monitor].handle);
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> acquired_tex;
    if (desktop_resource) {
      hr = desktop_resource->QueryInterface(
          IID_PPV_ARGS(acquired_tex.GetAddressOf()));
      if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        continue;
      }
    } else {
      duplication_->ReleaseFrame();
      continue;
    }

    D3D11_TEXTURE2D_DESC src_desc{};
    acquired_tex->GetDesc(&src_desc);

    if (!staging_) {
      D3D11_TEXTURE2D_DESC staging_desc = src_desc;
      staging_desc.Usage = D3D11_USAGE_STAGING;
      staging_desc.BindFlags = 0;
      staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      staging_desc.MiscFlags = 0;
      hr = d3d_device_->CreateTexture2D(&staging_desc, nullptr,
                                        staging_.GetAddressOf());
      if (FAILED(hr)) {
        LOG_ERROR("DXGI: CreateTexture2D staging failed, hr={}", (int)hr);
        duplication_->ReleaseFrame();
        continue;
      }
    }

    d3d_context_->CopyResource(staging_.Get(), acquired_tex.Get());

    bool cursor_embedded = false;
    const bool converted =
        ConvertFrame(frame_monitor, &cursor_embedded, cursor_compositor);

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
    duplication_->ReleaseFrame();
    capture_lock.unlock();

    if (converted && frame_monitor == monitor_index_.load() && callback_) {
      CapturedCursorFrameScope cursor_scope(cursor_embedded);
      callback_(nv12_frame_, nv12_width_ * nv12_height_ * 3 / 2, nv12_width_,
                nv12_height_, stream_id.c_str(), nullptr);
      cached_cursor_embedded = cursor_embedded;
      cached_monitor = frame_monitor;
      cached_frame_valid = true;
    }
  }
}

}  // namespace crossdesk
