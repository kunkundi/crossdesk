#include "screen_capturer_dxgi.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

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

std::string CleanDisplayName(const std::wstring& wide_name) {
  std::string name = WideToUtf8(wide_name);
  name.erase(std::remove_if(name.begin(), name.end(),
                            [](unsigned char c) { return !std::isalnum(c); }),
             name.end());
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
    return -2;
  }
  paused_ = false;
  LOG_INFO("DXGI: switched to monitor {}:{}", monitor_index_.load(),
           display_info_list_[monitor_index_].name);
  return 0;
}

int ScreenCapturerDxgi::ResetToInitialMonitor() {
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
      std::string name = CleanDisplayName(desc.DeviceName);
      MONITORINFOEX mi{};
      mi.cbSize = sizeof(MONITORINFOEX);
      if (GetMonitorInfo(desc.Monitor, &mi)) {
        bool is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? true : false;
        DisplayInfo info((void*)desc.Monitor, name, is_primary,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right, mi.rcMonitor.bottom);
        // primary first
        if (is_primary)
          display_info_list_.insert(display_info_list_.begin(), info);
        else
          display_info_list_.push_back(info);
        outputs_.push_back(output);
      }
    }
  }
}

bool ScreenCapturerDxgi::CreateDuplicationForMonitor(int monitor_index) {
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
  return true;
}

void ScreenCapturerDxgi::ReleaseDuplication() {
  staging_.Reset();
  if (duplication_) {
    duplication_->ReleaseFrame();
  }
  duplication_.Reset();
}

void ScreenCapturerDxgi::CaptureLoop() {
  const int timeout_ms = 33;
  while (running_) {
    if (paused_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (!duplication_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktop_resource;
    HRESULT hr = duplication_->AcquireNextFrame(
        timeout_ms, &frame_info, desktop_resource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
      continue;
    }
    if (FAILED(hr)) {
      LOG_ERROR("DXGI: AcquireNextFrame failed, hr={}", (int)hr);
      // attempt to recreate duplication
      ReleaseDuplication();
      CreateDuplicationForMonitor(monitor_index_);
      continue;
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

    int logical_width = static_cast<int>(src_desc.Width);
    int even_width = logical_width & ~1;
    int even_height = static_cast<int>(src_desc.Height) & ~1;
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

    libyuv::ARGBToNV12(static_cast<const uint8_t*>(mapped.pData),
                       static_cast<int>(mapped.RowPitch), nv12_frame_,
                       even_width, nv12_frame_ + even_width * even_height,
                       even_width, even_width, even_height);

    if (callback_) {
      callback_(nv12_frame_, nv12_size, even_width, even_height,
                display_info_list_[monitor_index_].name.c_str());
    }

    d3d_context_->Unmap(staging_.Get(), 0);
    duplication_->ReleaseFrame();
  }
}

}  // namespace crossdesk