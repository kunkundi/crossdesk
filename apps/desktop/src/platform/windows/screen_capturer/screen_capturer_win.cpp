#include "screen_capturer_win.h"

#include <Windows.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <display_stream_id.h>
#include "captured_nv12_frame.h"
#include "interactive_state.h"
#include "named_pipe_deadline.h"
#include "rd_log.h"
#include "screen_capturer_dxgi.h"
#include "screen_capturer_gdi.h"
#include "secure_desktop_frame_schedule.h"
#include "secure_desktop_status_poller.h"
#include "service_host.h"
#include "session_helper_shared.h"
#include "wgc_plugin_api.h"

namespace crossdesk {

namespace {

using Json = nlohmann::json;

constexpr DWORD kSecureDesktopStatusIntervalMs = 250;
constexpr DWORD kSecureDesktopStatusPipeTimeoutMs = 500;
constexpr DWORD kSecureDesktopHelperPipeTimeoutMs = 120;
constexpr DWORD kSecureDesktopTransientErrorGraceMs = 1500;
constexpr DWORD kSecureDesktopTransientErrorLogIntervalMs = 5000;
constexpr DWORD kPostSecureDesktopRestartRetryMs = 500;
constexpr DWORD kPostSecureDesktopRestartTimeoutMs = 10000;
constexpr int kSecureDesktopCaptureMinFps = 30;
constexpr int kSecureDesktopCaptureMaxIntervalMs =
    1000 / kSecureDesktopCaptureMinFps;

class WgcPluginCapturer final : public ScreenCapturer {
 public:
  using CreateFn = ScreenCapturer* (*)();
  using DestroyFn = void (*)(ScreenCapturer*);

  static std::unique_ptr<ScreenCapturer> Create() {
    std::filesystem::path plugin_path;
    wchar_t module_path[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
      return nullptr;
    }
    plugin_path =
        std::filesystem::path(module_path).parent_path() / L"wgc_plugin.dll";

    HMODULE module = LoadLibraryW(plugin_path.c_str());
    if (!module) {
      return nullptr;
    }

    auto create_fn = reinterpret_cast<CreateFn>(
        GetProcAddress(module, "CrossDeskCreateWgcCapturer"));
    auto destroy_fn = reinterpret_cast<DestroyFn>(
        GetProcAddress(module, "CrossDeskDestroyWgcCapturer"));
    if (!create_fn || !destroy_fn) {
      FreeLibrary(module);
      return nullptr;
    }

    ScreenCapturer* impl = create_fn();
    if (!impl) {
      FreeLibrary(module);
      return nullptr;
    }

    return std::unique_ptr<ScreenCapturer>(
        new WgcPluginCapturer(module, impl, destroy_fn));
  }

  ~WgcPluginCapturer() override {
    if (impl_) {
      destroy_fn_(impl_);
      impl_ = nullptr;
    }
    if (module_) {
      FreeLibrary(module_);
      module_ = nullptr;
    }
  }

  int Init(const int fps, cb_desktop_data cb) override {
    return impl_ ? impl_->Init(fps, std::move(cb)) : -1;
  }
  int Destroy() override { return impl_ ? impl_->Destroy() : 0; }
  int Start(bool show_cursor) override {
    return impl_ ? impl_->Start(show_cursor) : -1;
  }
  int Stop() override { return impl_ ? impl_->Stop() : 0; }
  int Pause(int monitor_index) override {
    return impl_ ? impl_->Pause(monitor_index) : -1;
  }
  int Resume(int monitor_index) override {
    return impl_ ? impl_->Resume(monitor_index) : -1;
  }
  std::vector<DisplayInfo> GetDisplayInfoList() override {
    return impl_ ? impl_->GetDisplayInfoList() : std::vector<DisplayInfo>{};
  }
  int SwitchTo(int monitor_index) override {
    return impl_ ? impl_->SwitchTo(monitor_index) : -1;
  }
  int ResetToInitialMonitor() override {
    return impl_ ? impl_->ResetToInitialMonitor() : -1;
  }

 private:
  WgcPluginCapturer(HMODULE module, ScreenCapturer* impl, DestroyFn destroy_fn)
      : module_(module), impl_(impl), destroy_fn_(destroy_fn) {}

  HMODULE module_ = nullptr;
  ScreenCapturer* impl_ = nullptr;
  DestroyFn destroy_fn_ = nullptr;
};

std::string BuildSecureCaptureCommand(int left, int top, int width, int height,
                                      bool show_cursor,
                                      const std::string& stage,
                                      const std::string& desktop) {
  std::ostringstream stream;
  stream << kCrossDeskSecureInputCaptureCommandPrefix << left << ":" << top
         << ":" << width << ":" << height << ":" << (show_cursor ? 1 : 0);
  if (!stage.empty()) {
    stream << ":" << stage;
    if (!desktop.empty()) {
      stream << ":" << desktop;
    }
  }
  return stream.str();
}

std::string BuildSecureCaptureStartCommand(int left, int top, int width,
                                           int height, bool show_cursor,
                                           int fps,
                                           const std::string& stage,
                                           const std::string& desktop) {
  std::ostringstream stream;
  stream << kCrossDeskSecureInputCaptureStartCommandPrefix << left << ":" << top
         << ":" << width << ":" << height << ":" << (show_cursor ? 1 : 0)
         << ":" << fps;
  if (!stage.empty()) {
    stream << ":" << stage;
    if (!desktop.empty()) {
      stream << ":" << desktop;
    }
  }
  return stream.str();
}

std::string ExtractPipeTextResponse(const std::vector<uint8_t>& response) {
  if (response.empty() || response.front() != '{') {
    return "<non-text-response>";
  }
  return std::string(response.begin(), response.end());
}

bool IsTransientSecureDesktopFrameError(const std::string& error_message) {
  return error_message.rfind("pipe_unavailable:", 0) == 0 ||
         error_message.find("\"error\":\"bitblt_failed\"") != std::string::npos;
}

bool IsTransientWindowsServiceStatusError(const std::string& error) {
  return error == "pipe_unavailable" || error == "pipe_connect_failed" ||
         error == "pipe_read_failed";
}

bool ParseSecureDesktopFrameResponse(const std::vector<uint8_t>& response,
                                     std::vector<uint8_t>* nv12_frame_out,
                                     int* width_out, int* height_out,
                                     std::string* error_out) {
  if (nv12_frame_out == nullptr || width_out == nullptr ||
      height_out == nullptr) {
    return false;
  }

  if (response.size() < sizeof(CrossDeskSecureDesktopFrameHeader)) {
    if (error_out != nullptr) {
      *error_out = ExtractPipeTextResponse(response);
    }
    return false;
  }

  CrossDeskSecureDesktopFrameHeader header{};
  std::memcpy(&header, response.data(), sizeof(header));
  if (header.magic != kCrossDeskSecureDesktopFrameMagic ||
      header.version != kCrossDeskSecureDesktopFrameVersion) {
    if (error_out != nullptr) {
      *error_out = ExtractPipeTextResponse(response);
    }
    return false;
  }

  const size_t expected_size = sizeof(header) + header.payload_size;
  if (expected_size != response.size()) {
    if (error_out != nullptr) {
      *error_out = "<invalid-frame-size>";
    }
    return false;
  }

  *width_out = static_cast<int>(header.width);
  *height_out = static_cast<int>(header.height);
  nv12_frame_out->assign(response.begin() + sizeof(header), response.end());
  return true;
}

SecureDesktopServiceStatus QuerySecureDesktopServiceStatus() {
  SecureDesktopServiceStatus status;
  std::vector<uint8_t> response;
  DWORD error_code = 0;
  if (!QueryNamedPipeWithDeadline(kCrossDeskServicePipeName, "status",
                                  kSecureDesktopStatusPipeTimeoutMs, &response,
                                  &status.error, &error_code)) {
    status.error_code = error_code;
    return status;
  }
  Json json = Json::parse(response.begin(), response.end(), nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    status.error = "invalid_service_status_json";
    return status;
  }

  status.service_available = json.value("ok", false);
  if (!status.service_available) {
    status.error = json.value("error", std::string("service_unavailable"));
    status.error_code = json.value("code", 0u);
    return status;
  }

  if (ShouldNormalizeUnlockToUserDesktop(
          json.value("interactive_lock_screen_visible", false),
          json.value("interactive_stage", std::string()),
          json.value("session_locked", false),
          json.value("interactive_logon_ui_visible", false),
          json.value("interactive_secure_desktop_active",
                     json.value("secure_desktop_active", false)),
          json.value("credential_ui_visible", false),
          json.value("password_box_visible", false),
          json.value("unlock_ui_visible", false),
          json.value("last_session_event", std::string()))) {
    status.active_session_id = json.value("active_session_id", 0xFFFFFFFFu);
    status.interactive_stage = "user-desktop";
    status.interactive_desktop.clear();
    status.capture_active = false;
    return status;
  }

  status.active_session_id = json.value("active_session_id", 0xFFFFFFFFu);
  status.helper_running = json.value("secure_input_helper_running", false);
  status.helper_process_id = json.value("secure_input_helper_pid", 0u);
  status.interactive_stage = json.value("interactive_stage", std::string());
  status.interactive_desktop =
      json.value("interactive_input_desktop", std::string());
  const bool secure_desktop_active =
      json.value("interactive_secure_desktop_active",
                 json.value("secure_desktop_active", false));
  status.capture_active =
      status.active_session_id != 0xFFFFFFFF &&
      (secure_desktop_active ||
       IsSecureDesktopInteractionRequired(status.interactive_stage));
  return status;
}

bool QuerySecureDesktopHelperCommand(DWORD session_id,
                                     const std::string& command,
                                     std::vector<uint8_t>* response_out,
                                     std::string* error_out) {
  if (response_out == nullptr) {
    return false;
  }

  const std::wstring pipe_name =
      GetCrossDeskSecureInputHelperPipeName(session_id);
  std::string error;
  DWORD code = 0;
  // The legacy single-frame path includes a full GDI capture and a large
  // response. Give it a separate deadline from the small control messages.
  const DWORD timeout_ms =
      command.rfind(kCrossDeskSecureInputCaptureCommandPrefix, 0) == 0
          ? 1000
          : kSecureDesktopHelperPipeTimeoutMs;
  const bool ok = QueryNamedPipeWithDeadline(pipe_name, command, timeout_ms,
                                             response_out, &error, &code);
  if (!ok && error_out != nullptr) {
    *error_out = error + ":" + std::to_string(code);
  }
  return ok;
}

bool QuerySecureDesktopHelperFrame(
    DWORD session_id, int left, int top, int width, int height,
    bool show_cursor, const std::string& stage, const std::string& desktop,
    std::vector<uint8_t>* nv12_frame_out, int* captured_width_out,
    int* captured_height_out, std::string* error_out) {
  if (nv12_frame_out == nullptr || captured_width_out == nullptr ||
      captured_height_out == nullptr) {
    return false;
  }

  const std::string command =
      BuildSecureCaptureCommand(left, top, width, height, show_cursor, stage,
                                desktop);
  std::vector<uint8_t> response;
  if (!QuerySecureDesktopHelperCommand(session_id, command, &response,
                                       error_out)) {
    return false;
  }

  return ParseSecureDesktopFrameResponse(response, nv12_frame_out,
                                         captured_width_out,
                                         captured_height_out, error_out);
}

}  // namespace

ScreenCapturerWin::ScreenCapturerWin() {}
ScreenCapturerWin::~ScreenCapturerWin() { Destroy(); }

int ScreenCapturerWin::Init(const int fps, cb_desktop_data cb) {
  fps_ = fps;
  cb_orig_ = cb;
  native_output_logged_.store(false, std::memory_order_relaxed);
  native_output_error_logged_.store(false, std::memory_order_relaxed);
  try {
    native_frame_pool_ = CapturedNv12FramePool::Create();
  } catch (...) {
    LOG_ERROR("Windows capturer: failed to create native NV12 frame pool");
    return -1;
  }
  cb_ = [this](unsigned char* data, int size, int w, int h,
               const char* reported_stream_id,
               const MiniRtcNativeVideoFrame* native_frame) {
    if (secure_desktop_capture_active_.load(std::memory_order_relaxed)) {
      return;
    }

    const char* raw_stream_id = reported_stream_id ? reported_stream_id : "";
    std::string mapped_stream_id;
    {
      std::lock_guard<std::mutex> lock(alias_mutex_);
      auto it = stream_id_alias_.find(raw_stream_id);
      if (it != stream_id_alias_.end()) {
        mapped_stream_id = it->second;
      } else {
        // Unknown backend labels are presentation data, not wire IDs.
        // Resolve them through the selected logical display instead.
        mapped_stream_id.clear();
      }
      mapped_stream_id = ResolveDisplayStreamId(
          mapped_stream_id.c_str(), canonical_displays_.size(),
          monitor_index_.load(std::memory_order_relaxed));
    }
    if (mapped_stream_id.empty()) {
      if (!invalid_stream_id_logged_.exchange(true,
                                              std::memory_order_relaxed)) {
        LOG_WARN("Windows capturer dropping frame without a registered stream "
                 "id: reported='{}', size={}x{}, bytes={}",
                 raw_stream_id, w, h, size);
      }
      return;
    }
    invalid_stream_id_logged_.store(false, std::memory_order_relaxed);
    if (post_secure_desktop_waiting_for_frame_.exchange(
            false, std::memory_order_relaxed)) {
      const ULONGLONG start_tick =
          post_secure_desktop_started_tick_.exchange(
              0, std::memory_order_relaxed);
      const ULONGLONG elapsed_ms =
          start_tick == 0 ? 0 : GetTickCount64() - start_tick;
      post_secure_desktop_drop_logged_.store(false,
                                             std::memory_order_relaxed);
      LOG_INFO(
          "Windows capturer first normal frame after secure desktop: "
          "reported_stream='{}', mapped_stream='{}', size={}x{}, bytes={}, "
          "elapsed_ms={}",
          raw_stream_id, mapped_stream_id, w, h, size, elapsed_ms);
    }
    EmitCapturedFrame(data, size, w, h, mapped_stream_id.c_str(),
                      native_frame);
  };

  int ret = -1;

  impl_ = WgcPluginCapturer::Create();
  impl_is_wgc_plugin_ = (impl_ != nullptr);
  ret = impl_ ? impl_->Init(fps_, cb_) : -1;
  if (ret == 0) {
    LOG_INFO("Windows capturer: using WGC plugin");
    BuildCanonicalFromImpl();
    monitor_index_.store(0, std::memory_order_relaxed);
    initial_monitor_index_ = 0;
    return 0;
  }

  LOG_WARN("Windows capturer: WGC plugin init failed (ret={}), try DXGI", ret);
  impl_.reset();
  impl_is_wgc_plugin_ = false;

  impl_ = std::make_unique<ScreenCapturerDxgi>();
  impl_is_wgc_plugin_ = false;
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using DXGI Desktop Duplication");
    BuildCanonicalFromImpl();
    monitor_index_.store(0, std::memory_order_relaxed);
    initial_monitor_index_ = 0;
    return 0;
  }

  LOG_WARN("Windows capturer: DXGI init failed (ret={}), fallback to GDI", ret);
  impl_.reset();

  impl_ = std::make_unique<ScreenCapturerGdi>();
  impl_is_wgc_plugin_ = false;
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using GDI BitBlt");
    BuildCanonicalFromImpl();
    monitor_index_.store(0, std::memory_order_relaxed);
    initial_monitor_index_ = 0;
    return 0;
  }

  LOG_ERROR("Windows capturer: all implementations failed, ret={}", ret);
  impl_.reset();
  return -1;
}

int ScreenCapturerWin::Destroy() {
  Stop();
  paused_.store(false, std::memory_order_relaxed);
  if (impl_) {
    impl_->Destroy();
    impl_.reset();
    impl_is_wgc_plugin_ = false;
  }
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    stream_id_alias_.clear();
    handle_to_canonical_index_.clear();
  }
  native_frame_pool_.reset();
  return 0;
}

void ScreenCapturerWin::EmitCapturedFrame(
    unsigned char* data, int size, int width, int height,
    const char* stream_id, const MiniRtcNativeVideoFrame* native_frame) {
  if (!cb_orig_) {
    return;
  }

  if (native_frame) {
    if (!native_output_logged_.exchange(true, std::memory_order_relaxed)) {
      LOG_INFO("Windows capturer native frame output enabled (type={})",
               static_cast<uint32_t>(native_frame->type));
    }
    cb_orig_(data, size, width, height, stream_id, native_frame);
    return;
  }

  CapturedNv12Frame* owned_frame = nullptr;
  if (native_frame_pool_ && data && size > 0 && width > 0 && height > 0) {
    owned_frame = native_frame_pool_->CopyFrom(
        data, static_cast<size_t>(size), static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
  }
  if (!owned_frame) {
    if (!native_output_error_logged_.exchange(true,
                                               std::memory_order_relaxed)) {
      LOG_WARN(
          "Windows capturer could not retain a native NV12 frame; falling "
          "back to copied CPU input (size={}x{}, bytes={})",
          width, height, size);
    }
    cb_orig_(data, size, width, height, stream_id, nullptr);
    return;
  }

  if (!native_output_logged_.exchange(true, std::memory_order_relaxed)) {
    LOG_INFO("Windows capturer native NV12 output enabled");
  }
  cb_orig_(nullptr, static_cast<int>(owned_frame->Size()), width, height,
           stream_id, owned_frame->Descriptor());
  owned_frame->Release();
}

int ScreenCapturerWin::Start(bool show_cursor) {
  if (!impl_) return -1;
  if (running_.load(std::memory_order_relaxed)) {
    return 0;
  }

  show_cursor_.store(show_cursor, std::memory_order_relaxed);
  paused_.store(false, std::memory_order_relaxed);
  invalid_stream_id_logged_.store(false, std::memory_order_relaxed);

  // Refresh physical monitor identities before every session. HMONITOR and
  // DXGI output handles may change after an HDMI hotplug while CrossDesk stays
  // open; logical stream IDs must remain stable.
  const int requested_monitor =
      monitor_index_.load(std::memory_order_relaxed);
  impl_->Destroy();
  int ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    RebuildAliasesFromImpl();
    ret = impl_->Start(show_cursor);
    if (ret == 0 && requested_monitor > 0 &&
        impl_->SwitchTo(requested_monitor) != 0) {
      monitor_index_.store(0, std::memory_order_relaxed);
    }
  }
  if (ret != 0) {
    LOG_WARN("Windows capturer: refresh/start failed (ret={}), trying fallback",
             ret);

    auto try_init_start = [&](std::unique_ptr<ScreenCapturer> cand) -> bool {
      int r = cand->Init(fps_, cb_);
      if (r != 0) return false;
      int s = cand->Start(show_cursor);
      if (s == 0) {
        impl_ = std::move(cand);
        impl_is_wgc_plugin_ = false;
        RebuildAliasesFromImpl();
        if (requested_monitor > 0 &&
            impl_->SwitchTo(requested_monitor) != 0) {
          monitor_index_.store(0, std::memory_order_relaxed);
        }
        return true;
      }
      return false;
    };

    bool fallback_started = false;
    if (impl_is_wgc_plugin_) {
      if (try_init_start(std::make_unique<ScreenCapturerDxgi>())) {
        LOG_INFO("Windows capturer: fallback to DXGI");
        fallback_started = true;
      } else if (try_init_start(std::make_unique<ScreenCapturerGdi>())) {
        LOG_INFO("Windows capturer: fallback to GDI");
        fallback_started = true;
      }
    } else if (dynamic_cast<ScreenCapturerDxgi*>(impl_.get())) {
      if (try_init_start(std::make_unique<ScreenCapturerGdi>())) {
        LOG_INFO("Windows capturer: fallback to GDI");
        fallback_started = true;
      }
    }

    if (!fallback_started) {
      LOG_ERROR("Windows capturer: all fallbacks failed to start");
      return ret;
    }
  }

  running_.store(true, std::memory_order_relaxed);
  secure_desktop_capture_active_.store(false, std::memory_order_relaxed);
  post_secure_desktop_waiting_for_frame_.store(false,
                                               std::memory_order_relaxed);
  post_secure_desktop_drop_logged_.store(false, std::memory_order_relaxed);
  post_secure_desktop_started_tick_.store(0, std::memory_order_relaxed);
  if (!secure_capture_thread_.joinable()) {
    secure_capture_thread_ =
        std::thread([this]() { SecureDesktopCaptureLoop(); });
  }
  return 0;
}

int ScreenCapturerWin::Stop() {
  running_.store(false, std::memory_order_relaxed);
  secure_desktop_capture_active_.store(false, std::memory_order_relaxed);
  post_secure_desktop_waiting_for_frame_.store(false,
                                               std::memory_order_relaxed);
  post_secure_desktop_drop_logged_.store(false, std::memory_order_relaxed);
  post_secure_desktop_started_tick_.store(0, std::memory_order_relaxed);
  int ret = 0;
  if (impl_) {
    ret = impl_->Stop();
  }
  StopSecureCaptureThread();
  StopSecureDesktopSharedCapture(secure_shared_session_id_);
  return ret;
}

int ScreenCapturerWin::Pause(int monitor_index) {
  paused_.store(true, std::memory_order_relaxed);
  if (!impl_) return -1;
  return impl_->Pause(monitor_index);
}

int ScreenCapturerWin::Resume(int monitor_index) {
  paused_.store(false, std::memory_order_relaxed);
  if (!impl_) return -1;
  return impl_->Resume(monitor_index);
}

int ScreenCapturerWin::SwitchTo(int monitor_index) {
  if (!impl_) return -1;
  const int ret = impl_->SwitchTo(monitor_index);
  if (ret == 0) {
    monitor_index_.store(monitor_index, std::memory_order_relaxed);
  }
  return ret;
}

int ScreenCapturerWin::ResetToInitialMonitor() {
  if (!impl_) return -1;
  const int ret = impl_->ResetToInitialMonitor();
  if (ret == 0) {
    monitor_index_.store(initial_monitor_index_, std::memory_order_relaxed);
  }
  return ret;
}

std::vector<DisplayInfo> ScreenCapturerWin::GetDisplayInfoList() {
  if (!impl_) return {};
  std::lock_guard<std::mutex> lock(alias_mutex_);
  return canonical_displays_;
}

void ScreenCapturerWin::BuildCanonicalFromImpl() {
  std::lock_guard<std::mutex> lock(alias_mutex_);
  handle_to_canonical_index_.clear();
  stream_id_alias_.clear();
  canonical_displays_ = impl_->GetDisplayInfoList();
  std::unordered_map<std::string, size_t> name_counts;
  for (const auto& display : canonical_displays_) {
    if (!display.name.empty()) {
      ++name_counts[display.name];
    }
  }
  for (size_t i = 0; i < canonical_displays_.size(); ++i) {
    auto& di = canonical_displays_[i];
    const std::string stream_id = MakeDisplayStreamId(i);
    if (di.name.empty()) {
      di.name = stream_id;
    }
    handle_to_canonical_index_[di.handle] = i;
    stream_id_alias_[stream_id] = stream_id;
    if (name_counts[di.name] == 1) {
      // Compatibility with an older WGC plugin that reports display names.
      stream_id_alias_[di.name] = stream_id;
    }
  }
}

void ScreenCapturerWin::RebuildAliasesFromImpl() {
  std::lock_guard<std::mutex> lock(alias_mutex_);
  stream_id_alias_.clear();
  auto current = impl_->GetDisplayInfoList();
  if (current.empty() || canonical_displays_.empty()) return;
  const auto previous_handles = handle_to_canonical_index_;
  handle_to_canonical_index_.clear();
  std::unordered_map<std::string, size_t> name_counts;
  for (const auto& display : current) {
    if (!display.name.empty()) {
      ++name_counts[display.name];
    }
  }
  auto similar = [&](const DisplayInfo& a, const DisplayInfo& b) {
    int dl = std::abs(a.left - b.left);
    int dt = std::abs(a.top - b.top);
    int dw = std::abs(a.width - b.width);
    int dh = std::abs(a.height - b.height);
    return dl <= 10 && dt <= 10 && dw <= 20 && dh <= 20;
  };
  std::vector<bool> used(canonical_displays_.size(), false);
  for (size_t current_index = 0; current_index < current.size();
       ++current_index) {
    const auto& di = current[current_index];
    int canonical_index = -1;
    auto old_handle = previous_handles.find(di.handle);
    if (old_handle != previous_handles.end() &&
        old_handle->second < canonical_displays_.size() &&
        !used[old_handle->second]) {
      canonical_index = static_cast<int>(old_handle->second);
    }
    if (canonical_index < 0) {
      for (size_t i = 0; i < canonical_displays_.size(); ++i) {
        if (!used[i] && (similar(di, canonical_displays_[i]) ||
                         (di.is_primary && canonical_displays_[i].is_primary))) {
          canonical_index = static_cast<int>(i);
          break;
        }
      }
    }
    if (canonical_index < 0 && current_index < canonical_displays_.size() &&
        !used[current_index]) {
      canonical_index = static_cast<int>(current_index);
    }
    if (canonical_index < 0) {
      LOG_WARN("Windows capturer ignoring unregistered display after topology "
               "change: display='{}', index={}",
               di.name, current_index);
      continue;
    }

    used[canonical_index] = true;
    auto& canonical = canonical_displays_[canonical_index];
    const std::string backend_stream_id =
        MakeDisplayStreamId(current_index);
    const std::string stable_stream_id =
        MakeDisplayStreamId(static_cast<size_t>(canonical_index));
    stream_id_alias_[backend_stream_id] = stable_stream_id;
    if (!di.name.empty() && name_counts[di.name] == 1) {
      stream_id_alias_[di.name] = stable_stream_id;
    }
    handle_to_canonical_index_[di.handle] =
        static_cast<size_t>(canonical_index);
    canonical = di;
    if (canonical.name.empty()) {
      canonical.name = stable_stream_id;
    }
  }
}

void ScreenCapturerWin::StopSecureCaptureThread() {
  if (secure_capture_thread_.joinable()) {
    secure_capture_thread_.join();
  }
}

bool ScreenCapturerWin::RestartCaptureBackendAfterSecureDesktop() {
  if (!impl_ || !running_.load(std::memory_order_relaxed)) {
    return false;
  }

  const bool show_cursor = show_cursor_.load(std::memory_order_relaxed);
  const int current_monitor = monitor_index_.load(std::memory_order_relaxed);
  auto restore_monitor = [&]() {
    RebuildAliasesFromImpl();
    if (current_monitor > 0 && impl_->SwitchTo(current_monitor) != 0) {
      monitor_index_.store(0, std::memory_order_relaxed);
    }
  };
  auto try_started_backend = [&](std::unique_ptr<ScreenCapturer> cand,
                                 const char* name,
                                 bool is_wgc_plugin) -> bool {
    if (!cand) {
      return false;
    }
    const int init_ret = cand->Init(fps_, cb_);
    if (init_ret != 0) {
      LOG_WARN("Windows capturer: {} init after secure desktop failed (ret={})",
               name, init_ret);
      return false;
    }
    const int start_ret = cand->Start(show_cursor);
    if (start_ret != 0) {
      LOG_WARN(
          "Windows capturer: {} start after secure desktop failed (ret={})",
          name, start_ret);
      cand->Destroy();
      return false;
    }
    if (impl_) {
      impl_->Destroy();
    }
    impl_ = std::move(cand);
    impl_is_wgc_plugin_ = is_wgc_plugin;
    restore_monitor();
    LOG_INFO("Windows capturer: restarted {} after secure desktop", name);
    return true;
  };

  LOG_INFO("Windows capturer: restarting capture backend after secure desktop");
  impl_->Stop();
  int ret = impl_->Start(show_cursor);
  if (ret == 0) {
    restore_monitor();
    return true;
  }

  LOG_WARN(
      "Windows capturer: capture backend restart after secure desktop failed "
      "(ret={}), rebuilding backend",
      ret);
  impl_->Destroy();
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    ret = impl_->Start(show_cursor);
  }
  if (ret == 0) {
    restore_monitor();
    return true;
  }

  if (impl_is_wgc_plugin_ &&
      try_started_backend(WgcPluginCapturer::Create(), "WGC plugin", true)) {
    return true;
  }
  if (try_started_backend(std::make_unique<ScreenCapturerDxgi>(), "DXGI",
                          false)) {
    return true;
  }
  if (try_started_backend(std::make_unique<ScreenCapturerGdi>(), "GDI",
                          false)) {
    return true;
  }

  if (impl_) {
    LOG_WARN(
        "Windows capturer: all backend restart attempts after secure desktop "
        "failed (last_ret={})",
        ret);
  }
  return false;
}

bool ScreenCapturerWin::GetCurrentCaptureRegion(int* left, int* top, int* width,
                                                int* height,
                                                std::string* display_name) {
  if (left == nullptr || top == nullptr || width == nullptr ||
      height == nullptr || display_name == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(alias_mutex_);
  if (canonical_displays_.empty()) {
    return false;
  }

  int current_monitor = monitor_index_.load(std::memory_order_relaxed);
  if (current_monitor < 0 ||
      current_monitor >= static_cast<int>(canonical_displays_.size())) {
    current_monitor = 0;
  }

  const auto& display = canonical_displays_[current_monitor];
  const int capture_width = display.width & ~1;
  const int capture_height = display.height & ~1;
  if (capture_width <= 0 || capture_height <= 0) {
    return false;
  }

  *left = display.left;
  *top = display.top;
  *width = capture_width;
  *height = capture_height;
  *display_name = MakeDisplayStreamId(static_cast<size_t>(current_monitor));
  return true;
}

void ScreenCapturerWin::CloseSecureDesktopSharedFrame() {
  if (secure_frame_view_ != nullptr) {
    UnmapViewOfFile(secure_frame_view_);
    secure_frame_view_ = nullptr;
  }
  if (secure_frame_ready_event_ != nullptr) {
    CloseHandle(secure_frame_ready_event_);
    secure_frame_ready_event_ = nullptr;
  }
  if (secure_frame_mapping_ != nullptr) {
    CloseHandle(secure_frame_mapping_);
    secure_frame_mapping_ = nullptr;
  }
  secure_frame_view_size_ = 0;
}

void ScreenCapturerWin::StopSecureDesktopSharedCapture(DWORD session_id) {
  DWORD target_session_id = session_id;
  if (target_session_id == 0xFFFFFFFF) {
    target_session_id = secure_shared_session_id_;
  }

  if (secure_shared_capture_started_ &&
      target_session_id != 0xFFFFFFFF) {
    std::vector<uint8_t> response;
    std::string error_message;
    QuerySecureDesktopHelperCommand(
        target_session_id, kCrossDeskSecureInputCaptureStopCommand, &response,
        &error_message);
  }

  CloseSecureDesktopSharedFrame();
  secure_shared_capture_started_ = false;
  secure_shared_session_id_ = 0xFFFFFFFF;
  secure_shared_left_ = 0;
  secure_shared_top_ = 0;
  secure_shared_width_ = 0;
  secure_shared_height_ = 0;
  secure_shared_fps_ = 0;
  secure_shared_show_cursor_ = true;
  secure_shared_stage_.clear();
  secure_shared_desktop_.clear();
}

bool ScreenCapturerWin::OpenSecureDesktopSharedFrame(DWORD session_id,
                                                     size_t min_size,
                                                     std::string* error_out) {
  if (secure_frame_view_ != nullptr &&
      secure_shared_session_id_ == session_id &&
      secure_frame_view_size_ >= min_size) {
    return true;
  }

  CloseSecureDesktopSharedFrame();

  const std::wstring mapping_name =
      GetCrossDeskSecureDesktopFrameMappingName(session_id);
  HANDLE frame_mapping =
      OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
  if (frame_mapping == nullptr) {
    if (error_out != nullptr) {
      *error_out = "open_frame_mapping_failed:" +
                   std::to_string(GetLastError());
    }
    return false;
  }

  auto* frame_view =
      static_cast<uint8_t*>(MapViewOfFile(frame_mapping, FILE_MAP_READ, 0, 0, 0));
  if (frame_view == nullptr) {
    const DWORD error = GetLastError();
    CloseHandle(frame_mapping);
    if (error_out != nullptr) {
      *error_out = "map_frame_view_failed:" + std::to_string(error);
    }
    return false;
  }

  const std::wstring event_name =
      GetCrossDeskSecureDesktopFrameReadyEventName(session_id);
  HANDLE frame_ready_event =
      OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str());
  if (frame_ready_event == nullptr) {
    const DWORD error = GetLastError();
    UnmapViewOfFile(frame_view);
    CloseHandle(frame_mapping);
    if (error_out != nullptr) {
      *error_out = "open_frame_event_failed:" + std::to_string(error);
    }
    return false;
  }

  secure_frame_mapping_ = frame_mapping;
  secure_frame_ready_event_ = frame_ready_event;
  secure_frame_view_ = frame_view;
  secure_frame_view_size_ = min_size;
  secure_shared_session_id_ = session_id;
  return true;
}

bool ScreenCapturerWin::ReadSecureDesktopSharedFrame(
    DWORD wait_ms, std::vector<uint8_t>* nv12_frame_out, int* width_out,
    int* height_out, std::string* error_out) {
  if (nv12_frame_out == nullptr || width_out == nullptr ||
      height_out == nullptr || secure_frame_view_ == nullptr ||
      secure_frame_ready_event_ == nullptr) {
    return false;
  }

  const DWORD wait_result = WaitForSingleObject(secure_frame_ready_event_,
                                                wait_ms);
  if (wait_result == WAIT_TIMEOUT) {
    if (error_out != nullptr) {
      *error_out = "frame_wait_timeout";
    }
    return false;
  }
  if (wait_result != WAIT_OBJECT_0) {
    if (error_out != nullptr) {
      *error_out = "frame_wait_failed:" + std::to_string(GetLastError());
    }
    return false;
  }

  auto* header =
      reinterpret_cast<CrossDeskSecureDesktopSharedFrameHeader*>(
          secure_frame_view_);
  if (header->magic != kCrossDeskSecureDesktopFrameMagic ||
      header->version != kCrossDeskSecureDesktopFrameVersion) {
    if (error_out != nullptr) {
      *error_out = "invalid_shared_frame_header";
    }
    return false;
  }
  if (header->writing != 0) {
    if (error_out != nullptr) {
      *error_out = "shared_frame_write_in_progress";
    }
    return false;
  }

  const uint32_t sequence = header->sequence;
  const uint32_t payload_size = header->payload_size;
  const uint32_t buffer_size = header->buffer_size;
  if (payload_size == 0 || payload_size > buffer_size ||
      sizeof(*header) + static_cast<size_t>(payload_size) >
          secure_frame_view_size_) {
    if (error_out != nullptr) {
      *error_out = "invalid_shared_frame_size";
    }
    return false;
  }

  nv12_frame_out->resize(payload_size);
  std::memcpy(nv12_frame_out->data(), secure_frame_view_ + sizeof(*header),
              payload_size);
  MemoryBarrier();
  if (header->writing != 0 || header->sequence != sequence) {
    if (error_out != nullptr) {
      *error_out = "shared_frame_changed_during_read";
    }
    return false;
  }

  *width_out = static_cast<int>(header->width);
  *height_out = static_cast<int>(header->height);
  return true;
}

bool ScreenCapturerWin::StartSecureDesktopSharedCapture(
    DWORD session_id, int left, int top, int width, int height,
    const std::string& stage, const std::string& desktop, bool show_cursor,
    int fps,
    std::string* error_out) {
  const size_t payload_size = static_cast<size_t>(width) * height * 3 / 2;
  const size_t mapping_size =
      sizeof(CrossDeskSecureDesktopSharedFrameHeader) + payload_size;
  if (payload_size == 0) {
    if (error_out != nullptr) {
      *error_out = "invalid_capture_size";
    }
    return false;
  }

  if (secure_shared_capture_started_ &&
      secure_shared_session_id_ == session_id &&
      secure_shared_left_ == left && secure_shared_top_ == top &&
      secure_shared_width_ == width && secure_shared_height_ == height &&
      secure_shared_stage_ == stage && secure_shared_desktop_ == desktop &&
      secure_shared_show_cursor_ == show_cursor && secure_shared_fps_ == fps &&
      OpenSecureDesktopSharedFrame(session_id, mapping_size, error_out)) {
    return true;
  }

  StopSecureDesktopSharedCapture(secure_shared_session_id_);

  const std::string command =
      BuildSecureCaptureStartCommand(left, top, width, height, show_cursor, fps,
                                     stage, desktop);
  std::vector<uint8_t> response;
  if (!QuerySecureDesktopHelperCommand(session_id, command, &response,
                                       error_out)) {
    return false;
  }

  Json json = Json::parse(response.begin(), response.end(), nullptr, false);
  if (json.is_discarded() || !json.value("ok", false)) {
    if (error_out != nullptr) {
      *error_out = ExtractPipeTextResponse(response);
    }
    return false;
  }

  secure_shared_capture_started_ = true;
  secure_shared_session_id_ = session_id;
  secure_shared_left_ = left;
  secure_shared_top_ = top;
  secure_shared_width_ = width;
  secure_shared_height_ = height;
  secure_shared_show_cursor_ = show_cursor;
  secure_shared_fps_ = fps;
  secure_shared_stage_ = stage;
  secure_shared_desktop_ = desktop;

  if (!OpenSecureDesktopSharedFrame(session_id, mapping_size, error_out)) {
    StopSecureDesktopSharedCapture(session_id);
    return false;
  }

  return true;
}

void ScreenCapturerWin::SecureDesktopCaptureLoop() {
  const int frame_interval_ms =
      fps_ > 0 ? (std::min)(kSecureDesktopCaptureMaxIntervalMs, 1000 / fps_)
               : kSecureDesktopCaptureMaxIntervalMs;
  ULONGLONG last_error_tick = 0;
  ULONGLONG capture_stage_started_tick = 0;
  bool post_secure_restart_pending = false;
  ULONGLONG post_secure_restart_deadline_tick = 0;
  ULONGLONG last_post_secure_restart_tick = 0;
  SecureDesktopServiceStatus status;
  SecureDesktopStatusPoller status_poller(
      QuerySecureDesktopServiceStatus,
      std::chrono::milliseconds(kSecureDesktopStatusIntervalMs));
  SecureDesktopFrameSchedule frame_schedule;
  frame_schedule.Reset(GetTickCount64());
  ULONGLONG stats_started = GetTickCount64();
  unsigned shared_frames = 0, fallback_frames = 0, shared_waits = 0;
  unsigned shared_restarts = 0;
  int64_t max_status_ms = 0;
  auto report_stats = [&](bool force) {
    const ULONGLONG elapsed = GetTickCount64() - stats_started;
    if (elapsed == 0 || (!force && elapsed < 5000)) return;
    if (shared_frames || fallback_frames || shared_waits || shared_restarts) {
      LOG_INFO(
          "Secure capture delivery: shared_fps={:.1f}, fallback_fps={:.1f}, "
          "pending_waits={}, shared_restarts={}, status_max_ms={}",
          shared_frames * 1000.0 / elapsed, fallback_frames * 1000.0 / elapsed,
          shared_waits, shared_restarts, max_status_ms);
    }
    stats_started = GetTickCount64();
    shared_frames = fallback_frames = shared_waits = shared_restarts = 0;
    max_status_ms = 0;
  };
  std::vector<uint8_t> secure_frame;

  while (running_.load(std::memory_order_relaxed)) {
    if (paused_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    const ULONGLONG now = GetTickCount64();
    const auto frame_started = std::chrono::steady_clock::now();
    report_stats(false);
    if (auto sample = status_poller.Take()) {
      const auto previous = std::exchange(status, std::move(sample->status));
      max_status_ms = (std::max)(max_status_ms, sample->query_ms);
      if (status.service_available != previous.service_available ||
          status.error != previous.error) {
        if (status.service_available) {
          LOG_INFO(
              "Windows capturer secure desktop service available, "
              "polling session_id={}",
              status.active_session_id);
        } else if (IsTransientWindowsServiceStatusError(status.error)) {
          LOG_INFO(
              "Windows capturer secure desktop service temporarily "
              "unavailable; "
              "keeping last capture state: error={}, code={}",
              status.error, status.error_code);
        } else {
          LOG_WARN(
              "Windows capturer secure desktop service unavailable: "
              "error={}, code={}",
              status.error, status.error_code);
        }
      }

      secure_desktop_capture_active_.store(status.capture_active,
                                           std::memory_order_relaxed);
      if (status.capture_active != previous.capture_active ||
          status.interactive_stage != previous.interactive_stage ||
          status.active_session_id != previous.active_session_id ||
          status.helper_process_id != previous.helper_process_id) {
        report_stats(true);
        // A restarted helper can leave our mapping/event handles alive, but
        // their old producer is gone. Explicitly establish a fresh stream.
        StopSecureDesktopSharedCapture(secure_shared_session_id_);
        frame_schedule.Reset(now);
        const bool secure_capture_started =
            !previous.capture_active && status.capture_active;
        const bool secure_capture_ended =
            previous.capture_active && !status.capture_active;
        capture_stage_started_tick = now;
        LOG_INFO(
            "Windows capturer secure desktop state: active={}, stage='{}', "
            "session_id={}",
            status.capture_active, status.interactive_stage,
            status.active_session_id);
        if (secure_capture_started) {
          post_secure_restart_pending = false;
          post_secure_desktop_waiting_for_frame_.store(
              false, std::memory_order_relaxed);
          post_secure_desktop_drop_logged_.store(false,
                                                 std::memory_order_relaxed);
          post_secure_desktop_started_tick_.store(0, std::memory_order_relaxed);
        } else if (secure_capture_ended) {
          post_secure_restart_pending = true;
          post_secure_restart_deadline_tick =
              now + kPostSecureDesktopRestartTimeoutMs;
          last_post_secure_restart_tick = 0;
          post_secure_desktop_waiting_for_frame_.store(
              true, std::memory_order_relaxed);
          post_secure_desktop_drop_logged_.store(false,
                                                 std::memory_order_relaxed);
          post_secure_desktop_started_tick_.store(now,
                                                  std::memory_order_relaxed);
        }
      }
    }

    if (!status.capture_active || status.active_session_id == 0xFFFFFFFF) {
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      if (post_secure_restart_pending) {
        if (now >= post_secure_restart_deadline_tick) {
          LOG_WARN(
              "Windows capturer: capture backend restart after secure desktop "
              "timed out");
          post_secure_restart_pending = false;
        } else if (last_post_secure_restart_tick == 0 ||
                   now - last_post_secure_restart_tick >=
                       kPostSecureDesktopRestartRetryMs) {
          last_post_secure_restart_tick = now;
          post_secure_restart_pending =
              !RestartCaptureBackendAfterSecureDesktop();
        }
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(status.service_available ? 50 : 200));
      continue;
    }

    if (!status.helper_running) {
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      continue;
    }

    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::string display_name;
    if (!GetCurrentCaptureRegion(&left, &top, &width, &height, &display_name)) {
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    int captured_width = 0;
    int captured_height = 0;
    std::string error_message;
    bool frame_delivered = false;
    const bool show_cursor = show_cursor_.load(std::memory_order_relaxed);
    const int shared_fps = fps_ > 0
                               ? (std::max)(kSecureDesktopCaptureMinFps, fps_)
                               : kSecureDesktopCaptureMinFps;

    if (secure_shared_capture_started_ &&
        frame_schedule.SharedCaptureStalled(now)) {
      LOG_WARN("Secure shared capture stalled; restarting producer");
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      frame_schedule.Reset(GetTickCount64());
      ++shared_restarts;
    }

    bool shared_ready = false;
    const bool was_started = secure_shared_capture_started_;
    if (was_started || frame_schedule.CanStart(now)) {
      shared_ready = StartSecureDesktopSharedCapture(
          status.active_session_id, left, top, width, height,
          status.interactive_stage, status.interactive_desktop, show_cursor,
          shared_fps, &error_message);
      if (!shared_ready) {
        frame_schedule.OnStartFailure(GetTickCount64());
      } else if (!was_started) {
        frame_schedule.Reset(GetTickCount64());
      }
    }

    if (shared_ready &&
        ReadSecureDesktopSharedFrame(
            static_cast<DWORD>((std::max)(50, frame_interval_ms + 20)),
            &secure_frame, &captured_width, &captured_height, &error_message)) {
      frame_delivered = true;
      ++shared_frames;
      frame_schedule.OnSharedFrame(GetTickCount64());
    }

    const bool frame_pending = shared_ready && !frame_delivered &&
                               IsPendingSecureDesktopFrame(error_message);
    if (frame_pending) ++shared_waits;
    if (shared_ready && !frame_delivered && !frame_pending) {
      StopSecureDesktopSharedCapture(secure_shared_session_id_);
      frame_schedule.OnStartFailure(GetTickCount64());
    }
    if (!frame_delivered && !frame_pending &&
        QuerySecureDesktopHelperFrame(
            status.active_session_id, left, top, width, height, show_cursor,
            status.interactive_stage, status.interactive_desktop, &secure_frame,
            &captured_width, &captured_height, &error_message)) {
      frame_delivered = true;
      ++fallback_frames;
    }

    if (frame_delivered && !secure_frame.empty()) {
      EmitCapturedFrame(secure_frame.data(),
                        static_cast<int>(secure_frame.size()), captured_width,
                        captured_height, display_name.c_str());
    }

    if (!frame_delivered && !frame_pending) {
      const bool transient_error =
          IsTransientSecureDesktopFrameError(error_message);
      const bool in_grace_period = capture_stage_started_tick != 0 &&
                                   now - capture_stage_started_tick <
                                       kSecureDesktopTransientErrorGraceMs;
      const DWORD log_interval =
          transient_error ? kSecureDesktopTransientErrorLogIntervalMs : 1000;
      if (transient_error && in_grace_period) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(frame_interval_ms));
        continue;
      }
      if (now - last_error_tick >= log_interval) {
        if (transient_error) {
          LOG_INFO(
              "Windows capturer secure desktop transient frame query failed, "
              "stage='{}', session_id={}, error={}",
              status.interactive_stage, status.active_session_id,
              error_message);
        } else {
          LOG_WARN(
              "Windows capturer secure desktop frame query failed, stage='{}', "
              "session_id={}, error={}",
              status.interactive_stage, status.active_session_id,
              error_message);
        }
        last_error_tick = now;
      }
    }

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - frame_started)
            .count();
    const int sleep_ms = SecureDesktopFrameSchedule::RemainingFrameDelay(
        frame_interval_ms, static_cast<uint64_t>(elapsed_ms));
    if (sleep_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
  }

  report_stats(true);
  StopSecureDesktopSharedCapture(secure_shared_session_id_);
  secure_desktop_capture_active_.store(false, std::memory_order_relaxed);
}

}  // namespace crossdesk
