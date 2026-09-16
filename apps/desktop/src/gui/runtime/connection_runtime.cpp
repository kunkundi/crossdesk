#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <display_stream_id.h>
#include "localization.h"
#include "platform.h"
#include "platform/video_renderer.h"
#include "rd_log.h"
#include "runtime/device_presence_request.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

namespace {
constexpr auto kPresenceProbeTimeout = std::chrono::seconds(5);
constexpr auto kPresenceRefreshInterval = std::chrono::seconds(30);
constexpr auto kPresenceRetryInterval = std::chrono::seconds(1);
}  // namespace

bool GuiRuntime::HasActiveSession() {
  if (stream_window_inited_) return true;
  std::shared_lock lock(connection_status_mutex_);
  return std::any_of(connection_status_.begin(), connection_status_.end(),
                     [](const auto& entry) {
                       return entry.second == ConnectionStatus::Connecting ||
                              entry.second == ConnectionStatus::Connected;
                     });
}

void GuiRuntime::HandleConnectionStatusChange() {
  if (!signal_connected_ || !peer_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now - last_presence_request_attempt_at_ < kPresenceRetryInterval) {
    return;
  }
  // Consume the flag before sending so a concurrent reconnect cannot lose its
  // refresh request. Keep it pending while disconnected or after a send error.
  const bool requested = need_to_send_recent_connections_.exchange(false);
  if (!requested && now < next_presence_refresh_at_) {
    return;
  }
  last_presence_request_attempt_at_ = now;
  const auto message =
      BuildDevicePresenceRequest(client_id_, recent_connection_ids_, true).dump();
  if (SendSignalMessage(peer_, message.data(), message.size()) == 0) {
    next_presence_refresh_at_ = now + kPresenceRefreshInterval;
  } else {
    need_to_send_recent_connections_ = true;
  }
}

void GuiRuntime::HandlePendingPresenceProbe() {
  bool has_action = false;
  bool should_connect = false;
  bool remember_password = false;
  std::string remote_id;
  std::string password;

  {
    std::lock_guard<std::mutex> lock(pending_presence_probe_mutex_);
    if (!pending_presence_probe_ || !pending_presence_result_ready_) {
      return;
    }

    has_action = true;
    should_connect = pending_presence_online_;
    remote_id = pending_presence_remote_id_;
    password = pending_presence_password_;
    remember_password = pending_presence_remember_password_;

    pending_presence_probe_ = false;
    pending_presence_result_ready_ = false;
    pending_presence_online_ = false;
    pending_presence_remote_id_.clear();
    pending_presence_password_.clear();
    pending_presence_remember_password_ = false;
  }

  if (!has_action) {
    return;
  }

  if (should_connect) {
    ConnectTo(remote_id, password.c_str(), remember_password, true);
    return;
  }

  offline_warning_text_ =
      localization::device_offline[localization_language_index_];
  show_offline_warning_window_ = true;
}

void GuiRuntime::HandlePresenceProbeTimeout() {
  const auto now = std::chrono::steady_clock::now();

  bool presence_probe_timed_out = false;
  std::string presence_remote_id;
  {
    std::lock_guard<std::mutex> lock(pending_presence_probe_mutex_);
    if (pending_presence_probe_ && !pending_presence_result_ready_ &&
        now - pending_presence_probe_started_at_ >= kPresenceProbeTimeout) {
      presence_probe_timed_out = true;
      presence_remote_id = pending_presence_remote_id_;
      pending_presence_probe_ = false;
      pending_presence_result_ready_ = false;
      pending_presence_online_ = false;
      pending_presence_remote_id_.clear();
      pending_presence_password_.clear();
      pending_presence_remember_password_ = false;
    }
  }

  if (presence_probe_timed_out) {
    offline_warning_text_ =
        localization::device_offline[localization_language_index_];
    show_offline_warning_window_ = true;
    LOG_WARN("Presence probe timed out for [{}]", presence_remote_id);
  }
}

void GuiRuntime::HandleServerControllerDisconnected(
    const std::string& remote_id, const char* reason) {
  keyboard_.ReleaseRemotePressedKeys(remote_id, reason);
#ifdef _WIN32
  devices_.ReleaseRemoteMouseButtons();
#endif
  {
    std::lock_guard lock(remote_pointer_input_mutex_);
    last_remote_pointer_input_time_.erase(remote_id);
  }

  bool has_connected_controller = false;
  bool has_web_controller = false;
  std::string remaining_controller_id;
  {
    std::unique_lock lock(connection_status_mutex_);
    connection_status_.erase(remote_id);
    connection_host_names_.erase(remote_id);
    privacy_sessions_.Disconnected(remote_id);
    for (const auto& [id, status] : connection_status_) {
      if (status != ConnectionStatus::Connected) {
        continue;
      }
      has_connected_controller = true;
      has_web_controller =
          has_web_controller || id.find("web") != std::string::npos;
      if (remaining_controller_id.empty()) {
        remaining_controller_id = id;
      }
    }
  }
  show_cursor_ = has_web_controller;
  if (has_connected_controller) {
    remote_client_id_ = remaining_controller_id;
    return;
  }

  LOG_INFO("Last remote controller disconnected; recovering privacy, reason={}",
           reason ? reason : "unknown");
  privacy_.Disconnected();

  need_to_create_server_window_.store(false, std::memory_order_release);
  need_to_destroy_server_window_.store(true, std::memory_order_release);
  is_server_mode_ = false;
#if defined(__linux__) && !defined(__APPLE__)
  if (IsWaylandSession()) {
    // Keep Wayland capture session warm to avoid black screen on subsequent
    // reconnects.
    start_screen_capturer_ = true;
    LOG_INFO(
        "Keeping Wayland screen capturer running after disconnect to "
        "preserve reconnect stability");
  } else {
    start_screen_capturer_ = false;
  }
#else
  start_screen_capturer_ = false;
#endif
  devices_.StopSpeakerCapturer();
  start_mouse_controller_ = false;
  start_keyboard_capturer_ = false;
  remote_client_id_.clear();
  audio_capture_ = false;
  devices_.ResetToInitialDisplay();
}

int GuiRuntime::RequestSingleDevicePresence(const std::string& remote_id,
                                            const char* password,
                                            bool remember_password) {
  if (!signal_connected_ || !peer_) {
    return -1;
  }

  {
    std::lock_guard<std::mutex> lock(pending_presence_probe_mutex_);
    pending_presence_probe_ = true;
    pending_presence_result_ready_ = false;
    pending_presence_online_ = false;
    pending_presence_probe_started_at_ = std::chrono::steady_clock::now();
    pending_presence_remote_id_ = remote_id;
    pending_presence_password_ = password ? password : "";
    pending_presence_remember_password_ = remember_password;
  }

  const auto s = BuildDevicePresenceRequest(
                     client_id_, recent_connection_ids_, false, remote_id)
                     .dump();

  int ret = SendSignalMessage(peer_, s.data(), s.size());
  if (ret != 0) {
    std::lock_guard<std::mutex> lock(pending_presence_probe_mutex_);
    pending_presence_probe_ = false;
    pending_presence_result_ready_ = false;
    pending_presence_online_ = false;
    pending_presence_remote_id_.clear();
    pending_presence_password_.clear();
    pending_presence_remember_password_ = false;
  }

  return ret;
}

void GuiRuntime::CloseRemoteSession(std::shared_ptr<RemoteSession> props) {
  if (!props || props->closing_.exchange(true)) return;
  if (props->peer_events_) props->peer_events_->Deactivate();
  props->connection_established_ = false;
  props->streaming_ = false;
  props->connection_status_.store(ConnectionStatus::Closed);
  std::shared_ptr<std::vector<unsigned char>> frame_snapshot;
  int video_width = 0;
  int video_height = 0;
  {
    std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
    frame_snapshot = props->front_frame_;
    video_width = props->video_width_;
    video_height = props->video_height_;
    if ((!frame_snapshot || frame_snapshot->empty()) &&
        props->thumbnail_frame_ && !props->thumbnail_frame_->empty()) {
      frame_snapshot = props->thumbnail_frame_;
      video_width = props->thumbnail_width_;
      video_height = props->thumbnail_height_;
    }
  }
  auto* native_renderer = video_renderer_.get();
  if ((!frame_snapshot || frame_snapshot->empty()) && native_renderer) {
    auto native_snapshot = std::make_shared<std::vector<unsigned char>>();
    if (native_renderer->CopyLatestNv12(props->remote_id_,
                                        native_snapshot.get(), &video_width,
                                        &video_height)) {
      frame_snapshot = std::move(native_snapshot);
    }
  }

  if (native_renderer) {
    native_renderer->DiscardStream(props->remote_id_);
    video_frame_dirty_.store(true, std::memory_order_release);
  }

  PeerPtr* peer = nullptr;
  {
    // Audio/clipboard senders hold a shared map lock while using this pointer.
    std::unique_lock lock(remote_sessions_mutex_);
    peer = std::exchange(props->peer_, nullptr);
  }
  const auto queued_at = std::chrono::steady_clock::now();
  session_cleanup_tasks_[props->remote_id_] = session_cleanup_queue_.PostTask(
      [props, peer, frame_snapshot, video_width, video_height,
       thumbnail = thumbnail_, queued_at]() mutable {
        if (peer) {
          LOG_INFO("[{}] Background leave connection [{}]", props->local_id_,
                   props->remote_id_);
          LeaveConnection(peer, props->remote_id_.c_str());
          DestroyPeer(&peer);
        }
        if (thumbnail && frame_snapshot && !frame_snapshot->empty() &&
            video_width > 0 && video_height > 0) {
          thumbnail->SaveToThumbnail(
              reinterpret_cast<char*>(frame_snapshot->data()), video_width,
              video_height, props->remote_id_, props->remote_host_name_,
              props->remember_password_ ? props->remote_password_ : "");
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - queued_at).count();
        LOG_INFO("[{}] Session cleanup completed in {} ms", props->remote_id_,
                 elapsed);
      });
}

void GuiRuntime::CloseServerController(const std::string& remote_id) {
  if (!peer_ || remote_id.empty() || controller_cleanup_tasks_.count(remote_id))
    return;
  controller_cleanup_tasks_[remote_id] = session_cleanup_queue_.PostTask(
      [peer = peer_, remote_id] { LeaveConnection(peer, remote_id.c_str()); });
}

void GuiRuntime::CloseConnectionPeer() {
  if (!peer_) return;
  peer_events_->Deactivate();
  std::vector<std::string> controllers;
  {
    std::shared_lock lock(connection_status_mutex_);
    for (const auto& [id, status] : connection_status_) controllers.push_back(id);
  }
  for (const auto& id : controllers)
    HandleServerControllerDisconnected(id, "peer_closed");
  devices_.StopMouseController();
  devices_.StopScreenCapturer();
  devices_.StopSpeakerCapturer();
  devices_.StopKeyboardCapturer();
  signal_connected_ = false;
  device_presence_cache_.SetSignalConnected(false);
  signal_status_ = SignalStatus::SignalClosed;
  PeerPtr* peer;
  {
    std::unique_lock lock(remote_sessions_mutex_);
    peer = std::exchange(peer_, nullptr);
  }
  // The same serial queue also owns outstanding controller disconnects, so
  // these finish before the shared peer is destroyed.
  connection_peer_cleanup_ = session_cleanup_queue_.PostTask(
      [peer, handler = peer_events_, id = std::string(client_id_)]() mutable {
        LOG_INFO("[{}] Background connection peer cleanup", id);
        LeaveConnection(peer, id.c_str());
        DestroyPeer(&peer);
      });
}

void GuiRuntime::CloseAllRemoteSessions() {
  CloseConnectionPeer();
  is_client_mode_ = false;
  pending_reconnects_.clear();
  std::vector<std::shared_ptr<RemoteSession>> sessions;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [id, props] : remote_sessions_) sessions.push_back(props);
  }
  for (const auto& props : sessions) CloseRemoteSession(props);
  {
    std::unique_lock lock(remote_sessions_mutex_);
    remote_sessions_.clear();
  }
}

void GuiRuntime::HandleSessionCleanup() {
  auto completed = [](std::future<void>& task) {
    if (!task.valid() || task.wait_for(std::chrono::seconds(0)) !=
                             std::future_status::ready) return false;
    try {
      task.get();
    } catch (const std::exception& error) {
      LOG_ERROR("Connection cleanup failed: {}", error.what());
    }
    return true;
  };
  completed(connection_peer_cleanup_);
  for (auto it = controller_cleanup_tasks_.begin();
       it != controller_cleanup_tasks_.end();) {
    if (completed(it->second)) it = controller_cleanup_tasks_.erase(it);
    else ++it;
  }
  std::vector<std::pair<std::string, PendingReconnect>> reconnects;
  for (auto it = session_cleanup_tasks_.begin();
       it != session_cleanup_tasks_.end();) {
    if (it->second.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
      ++it;
      continue;
    }
    try {
      it->second.get();
    } catch (const std::exception& error) {
      LOG_ERROR("[{}] Session cleanup failed: {}", it->first, error.what());
    }
    if (auto pending = pending_reconnects_.find(it->first);
        pending != pending_reconnects_.end()) {
      reconnects.emplace_back(pending->first, std::move(pending->second));
      pending_reconnects_.erase(pending);
    }
    it = session_cleanup_tasks_.erase(it);
    reload_recent_connections_ = true;
    recent_connection_image_save_time_ = 0;
  }
  for (const auto& [id, request] : reconnects) {
    ConnectTo(id, request.password.c_str(), request.remember_password);
  }
}

void GuiRuntime::WaitForSessionCleanup() {
  pending_reconnects_.clear();
  auto drain = [](std::future<void>& task) {
    if (!task.valid()) return;
    try {
      task.get();
    } catch (const std::exception& error) {
      LOG_ERROR("Connection cleanup failed: {}", error.what());
    }
  };
  drain(connection_peer_cleanup_);
  for (auto& [id, task] : controller_cleanup_tasks_) drain(task);
  controller_cleanup_tasks_.clear();
  for (auto& [id, task] : session_cleanup_tasks_) {
    try {
      task.get();
    } catch (const std::exception& error) {
      LOG_ERROR("[{}] Session cleanup failed: {}", id, error.what());
    }
  }
  session_cleanup_tasks_.clear();
}

void GuiRuntime::ResetRemoteSessionResources(
    std::shared_ptr<RemoteSession> props) {
  {
    std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
    props->front_frame_.reset();
    props->back_frame_.reset();
    props->thumbnail_frame_.reset();
    props->thumbnail_width_ = 0;
    props->thumbnail_height_ = 0;
    props->background_snapshot_time_ = {};
    props->video_width_ = 0;
    props->video_height_ = 0;
    props->video_size_ = 0;
    props->render_rect_dirty_ = true;
    props->stream_cleanup_pending_ = false;
  }
  {
    std::lock_guard<std::mutex> lock(props->remote_cursor_state_mutex_);
    props->remote_cursor_state_ = {};
    props->remote_cursor_state_received_ = false;
    props->cursor_presentation_ = {};
  }
}

std::shared_ptr<GuiRuntime::RemoteSession> GuiRuntime::FindRemoteSession(
    const std::string& remote_id) {
  if (remote_id.empty()) {
    return nullptr;
  }

  std::shared_lock lock(remote_sessions_mutex_);
  auto it = remote_sessions_.find(remote_id);
  if (it == remote_sessions_.end()) {
    return nullptr;
  }
  return it->second;
}

int GuiRuntime::ConnectTo(const std::string& remote_id, const char* password,
                          bool remember_password, bool bypass_presence_check) {
  if (session_cleanup_tasks_.count(remote_id)) {
    pending_reconnects_[remote_id] = {password ? password : "", remember_password};
    LOG_INFO("[{}] Reconnect queued until previous peer is destroyed", remote_id);
    return 0;
  }
  if (!bypass_presence_check && !device_presence_cache_.IsOnline(remote_id)) {
    int ret =
        RequestSingleDevicePresence(remote_id, password, remember_password);
    if (ret != 0) {
      offline_warning_text_ =
          localization::device_offline[localization_language_index_];
      show_offline_warning_window_ = true;
      LOG_WARN("Presence probe failed for [{}], ret={}", remote_id, ret);
    } else {
      LOG_INFO("Presence probe requested for [{}] before connect", remote_id);
    }
    return -1;
  }

  LOG_INFO("Connect to [{}]", remote_id);
  focused_remote_id_ = remote_id;

  bool exists = FindRemoteSession(remote_id) != nullptr;

  if (!exists) {
    PeerPtr* peer_to_init = nullptr;
    std::string local_id;

    {
      std::unique_lock unique_lock(remote_sessions_mutex_);
      if (remote_sessions_.find(remote_id) == remote_sessions_.end()) {
        remote_sessions_[remote_id] = std::make_shared<RemoteSession>();
        auto props = remote_sessions_[remote_id];
        props->local_id_ = "C-" + std::string(client_id_);
        props->remote_id_ = remote_id;
        memcpy(&props->params_, &params_, sizeof(Params));
        props->params_.user_id = props->local_id_.c_str();
        props->peer_events_ = std::make_shared<PeerEventHandler>(*this);
        props->params_.user_data = props->peer_events_.get();
        props->peer_ = CreatePeer(&props->params_);

        props->control_window_width_ = title_bar_height_ * 10.0f;
        props->control_window_height_ = title_bar_height_ * 1.3f;
        props->control_window_min_width_ = title_bar_height_ * 0.65f;
        props->control_window_min_height_ = title_bar_height_ * 1.3f;
        props->control_window_max_width_ = title_bar_height_ * 10.0f;
        props->control_window_max_height_ = title_bar_height_ * 7.0f;

        props->connection_status_.store(ConnectionStatus::Connecting);
        show_connection_status_window_ = true;

        if (!props->peer_) {
          LOG_INFO("Create peer [{}] instance failed", props->local_id_);
          return -1;
        }

        const auto& displays = devices_.display_info_list();
        for (size_t index = 0; index < displays.size(); ++index) {
          const std::string stream_id = MakeDisplayStreamId(index);
          AddVideoStream(props->peer_, stream_id.c_str());
        }
        AddAudioStream(props->peer_, props->audio_label_.c_str());
        AddDataStream(props->peer_, props->data_label_.c_str(), false);
        AddDataStream(props->peer_, props->mouse_label_.c_str(), false);
        AddDataStream(props->peer_, props->keyboard_label_.c_str(), true);
        AddDataStream(props->peer_, props->control_data_label_.c_str(), true);
        AddDataStream(props->peer_, props->file_label_.c_str(), true);
        AddDataStream(props->peer_, props->file_feedback_label_.c_str(), true);
        AddDataStream(props->peer_, props->clipboard_label_.c_str(), true);

        props->connection_status_.store(ConnectionStatus::Connecting);

        peer_to_init = props->peer_;
        local_id = props->local_id_;
      }
    }

    if (peer_to_init) {
      LOG_INFO("[{}] Create peer instance successful", local_id);
      Init(peer_to_init);
      LOG_INFO("[{}] Peer init finish", local_id);
    }
  }

  int ret = -1;
  auto props = FindRemoteSession(remote_id);
  if (!props || props->closing_) return -1;
  if (!props->connection_established_) {
    props->connection_status_.store(ConnectionStatus::Connecting);
    show_connection_status_window_ = true;

    props->remember_password_ = remember_password;
    if (strcmp(password, "") != 0 &&
        strcmp(password, props->remote_password_) != 0) {
      strncpy(props->remote_password_, password,
              sizeof(props->remote_password_) - 1);
      props->remote_password_[sizeof(props->remote_password_) - 1] = '\0';
    }

    std::string remote_id_with_pwd = remote_id + "@" + password;
    if (props->peer_) {
      ret = JoinConnection(props->peer_, remote_id_with_pwd.c_str());
      if (0 == ret) {
        props->rejoin_ = false;
      } else {
        props->rejoin_ = true;
        need_to_rejoin_ = true;
      }
    }
  }

  return 0;
}
}  // namespace crossdesk
