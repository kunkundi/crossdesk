#include "screen_capturer_dxgi.h"

#include <display_stream_id.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "dxgi_cursor_state.h"
#include "libyuv.h"
#include "rd_log.h"

namespace crossdesk {

namespace {
std::string WideToUtf8(const std::wstring& wstr) {
  if (wstr.empty()) return {};
  int size_needed = WideCharToMultiByte(
      CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
  std::string result(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), result.data(),
                      size_needed, nullptr, nullptr);
  return result;
}

std::string GetDisplayLabel(const std::wstring& wide_name) {
  std::string name = WideToUtf8(wide_name);
  constexpr char kDevicePrefix[] = "\\\\.\\";
  if (name.rfind(kDevicePrefix, 0) == 0) {
    name.erase(0, sizeof(kDevicePrefix) - 1);
  }
  return name;
}
}  // namespace

ScreenCapturerDxgi::ScreenCapturerDxgi() {}
ScreenCapturerDxgi::~ScreenCapturerDxgi() {
  Stop();
  Destroy();
}

int ScreenCapturerDxgi::Init(const int fps, cb_desktop_data cb) {
  fps_ = fps;
  callback_ = cb;
  if (!callback_) {
    LOG_ERROR("DXGI: callback is null");
    return -1;
  }

  if (!InitializeDxgi()) {
    LOG_ERROR("DXGI: initialize DXGI failed");
    return -2;
  }

  EnumerateDisplays();
  if (display_info_list_.empty()) {
    LOG_ERROR("DXGI: no displays found");
    return -3;
  }

  monitor_index_ = 0;
  initial_monitor_index_ = monitor_index_;
  return 0;
}

int ScreenCapturerDxgi::Destroy() {
  Stop();
  ReleaseDuplication();
  outputs_.clear();
  d3d_context_.Reset();
  d3d_device_.Reset();
  dxgi_factory_.Reset();
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
  paused_ = true;
  monitor_index_ = monitor_index;
  ReleaseDuplication();
  if (!CreateDuplicationForMonitor(monitor_index_)) {
    LOG_ERROR("DXGI: create duplication failed for monitor {}",
              monitor_index_.load());
    paused_ = false;  // Reset paused_ on failure
    return -2;
  }
  paused_ = false;
  LOG_INFO("DXGI: switched to monitor {}:{}", monitor_index_.load(),
           display_info_list_[monitor_index_].name);
  return 0;
}

int ScreenCapturerDxgi::ResetToInitialMonitor() {
  std::lock_guard<std::mutex> lock(switch_mutex_);
  if (display_info_list_.empty()) return -1;
  int target = initial_monitor_index_;
  if (target < 0 || target >= (int)display_info_list_.size()) return -1;
  if (monitor_index_ == target) return 0;
  if (running_) {
    paused_ = true;
    monitor_index_ = target;
    ReleaseDuplication();
    if (!CreateDuplicationForMonitor(monitor_index_)) {
      paused_ = false;
      return -2;
    }
    paused_ = false;
    LOG_INFO("DXGI: reset to initial monitor {}:{}", monitor_index_.load(),
             display_info_list_[monitor_index_].name);
  } else {
    monitor_index_ = target;
  }
  return 0;
}

bool ScreenCapturerDxgi::InitializeDxgi() {
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0};

  D3D_FEATURE_LEVEL out_level{};
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, feature_levels,
      ARRAYSIZE(feature_levels), D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
      &out_level, d3d_context_.GetAddressOf());
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                           feature_levels, ARRAYSIZE(feature_levels),
                           D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
                           &out_level, d3d_context_.GetAddressOf());
    if (FAILED(hr)) {
      LOG_ERROR("DXGI: D3D11CreateDevice failed, hr={}", (int)hr);
      return false;
    }
  }

  hr = CreateDXGIFactory1(
      __uuidof(IDXGIFactory1),
      reinterpret_cast<void**>(dxgi_factory_.GetAddressOf()));
  if (FAILED(hr)) {
    LOG_ERROR("DXGI: CreateDXGIFactory1 failed, hr={}", (int)hr);
    return false;
  }
  return true;
}

void ScreenCapturerDxgi::EnumerateDisplays() {
  display_info_list_.clear();
  outputs_.clear();

  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  for (UINT a = 0;
       dxgi_factory_->EnumAdapters(a, adapter.ReleaseAndGetAddressOf()) !=
       DXGI_ERROR_NOT_FOUND;
       ++a) {
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) !=
                     DXGI_ERROR_NOT_FOUND;
         ++o) {
      DXGI_OUTPUT_DESC desc{};
      if (FAILED(output->GetDesc(&desc))) {
        continue;
      }
      std::string name = GetDisplayLabel(desc.DeviceName);
      MONITORINFOEX mi{};
      mi.cbSize = sizeof(MONITORINFOEX);
      if (GetMonitorInfo(desc.Monitor, &mi)) {
        bool is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? true : false;
        DisplayInfo info((void*)desc.Monitor, name, is_primary,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right, mi.rcMonitor.bottom);
        // primary first
        if (is_primary) {
          display_info_list_.insert(display_info_list_.begin(), info);
          outputs_.insert(outputs_.begin(), output);
        } else {
          display_info_list_.push_back(info);
          outputs_.push_back(output);
        }
      }
    }
  }
}

bool ScreenCapturerDxgi::CreateDuplicationForMonitor(int monitor_index) {
  SharedDxgiCursorState().Reset();
  if (monitor_index < 0 || monitor_index >= (int)outputs_.size()) return false;
  Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
  HRESULT hr = outputs_[monitor_index]->QueryInterface(
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
  LOG_INFO("DXGI: duplication ready, monitor={}, rotation={}", monitor_index,
           static_cast<int>(rotation_));
  return true;
}

bool ScreenCapturerDxgi::RecreateDuplicationForCurrentMonitor() {
  std::lock_guard<std::mutex> lock(switch_mutex_);
  ReleaseDuplication();
  int current_monitor = monitor_index_.load();
  if (CreateDuplicationForMonitor(current_monitor)) {
    return true;
  }

  EnumerateDisplays();
  if (display_info_list_.empty()) {
    LOG_ERROR("DXGI: no displays found while recreating duplication");
    return false;
  }
  if (current_monitor < 0 ||
      current_monitor >= static_cast<int>(display_info_list_.size())) {
    current_monitor = 0;
    monitor_index_ = 0;
  }
  if (CreateDuplicationForMonitor(current_monitor)) {
    LOG_INFO("DXGI: recreated duplication for monitor {}",
             monitor_index_.load());
    return true;
  }
  return false;
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

void ScreenCapturerDxgi::CaptureLoop() {
  const int timeout_ms = (std::max)(1, 1000 / (std::max)(1, fps_));
  bool cached_frame_valid = false;
  int cached_monitor = -1;
  uint64_t cached_generation = 0;
  std::vector<uint8_t> rotated_frame;
  auto last_duplication_retry =
      std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);
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
    if (!duplication_) {
      cached_frame_valid = false;
      capture_lock.unlock();
      const auto now = std::chrono::steady_clock::now();
      if (now - last_duplication_retry >= std::chrono::milliseconds(500)) {
        last_duplication_retry = now;
        RecreateDuplicationForCurrentMonitor();
      }
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
        const auto stream_id = MakeDisplayStreamId(cached_monitor);
        capture_lock.unlock();
        callback_(nv12_frame_, nv12_width_ * nv12_height_ * 3 / 2, nv12_width_,
                  nv12_height_, stream_id.c_str(), nullptr);
      }
      continue;
    }
    // Never replay a cached image after an acquisition/conversion failure or
    // a backend rebuild. Normal error/reset handling releases optional privacy.
    cached_frame_valid = false;
    if (FAILED(hr)) {
      SharedDxgiCursorState().Reset();
      LOG_ERROR("DXGI: AcquireNextFrame failed, hr={}", (int)hr);
      capture_lock.unlock();
      if (callback_)
        callback_(nullptr, ScreenCapturer::kBackendReset, 0, 0, "", nullptr);
      RecreateDuplicationForCurrentMonitor();
      continue;
    }

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

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3d_context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
      duplication_->ReleaseFrame();
      continue;
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
      rotated_frame.resize(static_cast<size_t>(logical_width) * logical_height *
                           4);
      if (libyuv::ARGBRotate(pixels, stride, rotated_frame.data(),
                             rotated_width * 4, logical_width, logical_height,
                             rotation) != 0) {
        d3d_context_->Unmap(staging_.Get(), 0);
        duplication_->ReleaseFrame();
        continue;
      }
      pixels = rotated_frame.data();
      stride = rotated_width * 4;
      if (swap_axes) std::swap(logical_width, logical_height);
    }
    int even_width = logical_width & ~1;
    int even_height = logical_height & ~1;
    if (even_width <= 0 || even_height <= 0) {
      d3d_context_->Unmap(staging_.Get(), 0);
      duplication_->ReleaseFrame();
      continue;
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

    cached_generation = duplication_generation_;
    d3d_context_->Unmap(staging_.Get(), 0);
    duplication_->ReleaseFrame();
    capture_lock.unlock();

    if (converted == 0 && frame_monitor == monitor_index_.load() && callback_) {
      int idx = frame_monitor;
      if (idx >= 0 && idx < static_cast<int>(display_info_list_.size())) {
        const std::string stream_id = MakeDisplayStreamId(idx);
        callback_(nv12_frame_, nv12_size, even_width, even_height,
                  stream_id.c_str(), nullptr);
        cached_monitor = idx;
        cached_frame_valid = true;
      } else {
        LOG_ERROR("DXGI: CaptureLoop invalid monitor_index {} (list size {})",
                  idx, display_info_list_.size());
      }
    }
  }
}

}  // namespace crossdesk
