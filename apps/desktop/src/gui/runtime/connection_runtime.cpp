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
#include "screen_lock.h"
#include "runtime/device_presence_request.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

namespace {
constexpr auto kPresenceProbeTimeout = std::chrono::seconds(5);
constexpr auto kPresenceRefreshInterval = std::chrono::seconds(30);
constexpr auto kPresenceRetryInterval = std::chrono::seconds(1);
}  // namespace

bool GuiRuntime::HasActiveSession() {
  if (lan_ && !lan_->Quiescent()) return true;
  if (stream_window_inited_) return true;
  std::shared_lock lock(connection_status_mutex_);
  return std::any_of(connection_status_.begin(), connection_status_.end(),
                     [](const auto& entry) {
                       return entry.second == MiniRtcConnectionStatus::MiniRtcConnectionConnecting ||
                              entry.second == MiniRtcConnectionStatus::MiniRtcConnectionConnected;
                     });
}

void GuiRuntime::MuteRemoteAudio(const std::shared_ptr<RemoteSession>& props) {
  if (!props) return;
  std::lock_guard delivery_lock(audio_delivery_mutex_);
  props->audio_capture_button_pressed_ = false;
  props->audio_allowed_ = false;
  devices_.ClearAudio(props->remote_id_);
  // Capable peers stop sending to this controller on the next subscription;
  // old peers remain muted by the local receive gate above.
  props->preview_needs_sync_ = true;
}

void GuiRuntime::UnmuteRemoteAudio(const std::shared_ptr<RemoteSession>& props) {
  if (!props || props->closing_) return;
  props->audio_capture_button_pressed_ = true;
  props->preview_needs_sync_ = true;
  // Also resume capture on old peers, or after a legacy controller stopped it.
  RemoteAction action{};
  action.type = ControlType::audio_capture;
  action.a = true;
  const auto message = action.to_json();
  MiniRtcSendReliableDataFrame(props->peer_, message.data(), message.size(),
                        props->control_data_label_.c_str());
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
      BuildDevicePresenceRequest(client_id_, recent_connection_ids_, true, {},
                                 gallery_presence_ids_).dump();
  if (PeerSendSignalMessage(peer_, message.data(), message.size()) == 0) {
    next_presence_refresh_at_ = now + kPresenceRefreshInterval;
  } else {
    need_to_send_recent_connections_ = true;
  }
}

void GuiRuntime::HandlePendingRejoins() {
  need_to_rejoin_ = false;
  std::vector<std::shared_ptr<RemoteSession>> pending;
  {
    std::shared_lock lock(remote_sessions_mutex_);
    for (const auto& [_, props] : remote_sessions_) {
      if (props && props->rejoin_ && !props->closing_)
        pending.push_back(props);
    }
  }
  for (const auto& props : pending) {
    // Password submission already entered the validating state. Let Join
    // report its result even when this device is absent from the presence
    // cache (for example, a newly added DeskHub device). A failed presence
    // probe has no auth callback and would leave the prompt validating forever.
    ConnectTo(props->remote_id_, props->remote_password_,
              props->remember_password_, true, props->wall_managed_.load());
  }
}

void GuiRuntime::CancelPendingConnection(const std::string& remote_id) {
  pending_reconnects_.erase(remote_id);
  if (auto props = FindRemoteSession(remote_id)) props->rejoin_ = false;
  std::lock_guard lock(pending_presence_probe_mutex_);
  if (pending_presence_remote_id_ != remote_id) return;
  pending_presence_probe_ = false;
  pending_presence_result_ready_ = false;
  pending_presence_online_ = false;
  pending_presence_remote_id_.clear();
  pending_presence_password_.clear();
  pending_presence_remember_password_ = false;
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
    LOG_WARN("Presence probe timed out");
  }
}

void GuiRuntime::HandleServerControllerDisconnected(
    const std::string& remote_id, const char* reason) {
  if (auto events = ControllerEvents(remote_id))
    transfers_.Cancel(events.get(), remote_id);
  preview_subscriptions_.Remove(remote_id);

  bool has_connected_controller = false;
  std::string remaining_controller_id;
  {
    std::unique_lock lock(connection_status_mutex_);
    connection_status_.erase(remote_id);
    disconnect_lock_.Disconnected(remote_id,
        config_center_->IsLockOnDisconnect() &&
        !(managed_host_frontend_ && remote_id.rfind("lan/", 0) != 0));
    remote_input_.Disconnect(remote_id);
    devices_.OnControllerConnectionsChanged();
    connection_host_names_.erase(remote_id);
    codec_status_controllers_.erase(remote_id);
    privacy_sessions_.Disconnected(remote_id);
    for (const auto& [id, status] : connection_status_) {
      if (status != MiniRtcConnectionStatus::MiniRtcConnectionConnected) {
        continue;
      }
      has_connected_controller = true;
      if (remaining_controller_id.empty()) {
        remaining_controller_id = id;
      }
    }
#if _WIN32
    windows_rdp_handoff_connected_->store(has_connected_controller,
                                          std::memory_order_relaxed);
#endif
  }
  clipboard_.ForgetWeb(remote_id);
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
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
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
  {
    std::shared_lock lock(remote_sessions_mutex_);
    start_keyboard_capturer_ = std::any_of(
        remote_sessions_.begin(), remote_sessions_.end(), [](const auto& item) {
          const auto& session = item.second;
          return session && !session->closing_ &&
                 session->connection_established_ && !session->wall_managed_;
        });
  }
  remote_client_id_.clear();
  audio_capture_ = false;
}

void GuiRuntime::HandleScreenLock() {
  // Serialize request consumption with incoming admission so a reconnect
  // cancels a pending lock before it can interrupt the new controller.
  std::unique_lock lock(connection_status_mutex_);
  if (!disconnect_lock_.TakeRequest(config_center_->IsLockOnDisconnect()))
    return;
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
  if (LockScreen()) LOG_INFO("Requested screen lock after last controller disconnected");
#endif
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
                     client_id_, recent_connection_ids_, false, remote_id,
                     gallery_presence_ids_)
                     .dump();

  int ret = PeerSendSignalMessage(peer_, s.data(), s.size());
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

int GuiRuntime::ForceSignalReconnect() {
  if (!peer_) {
    LOG_WARN("Signal reconnect requested with no connection peer");
    return -1;
  }
  return PeerReconnectSignal(peer_);
}

void GuiRuntime::CloseRemoteSession(std::shared_ptr<RemoteSession> props) {
  if (!props) return;
  if (props->lan_session_ && !props->lan_session_->closing) {
    if (lan_) lan_->Close(props->lan_session_);
    return;
  }
  if (props->closing_.exchange(true)) return;
  MuteRemoteAudio(props);
  if (props->peer_events_) {
    props->peer_events_->StopAccepting();
    transfers_.Cancel(props->peer_events_.get());
  }
  props->connection_established_ = false;
  props->streaming_ = false;
  props->connection_status_.store(MiniRtcConnectionStatus::MiniRtcConnectionClosed);
  std::shared_ptr<std::vector<unsigned char>> frame_snapshot;
  int video_width = 0;
  int video_height = 0;
  const bool save_preview = config_center_->IsSaveRemotePreviews();
  if (save_preview) {
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
  if (save_preview && (!frame_snapshot || frame_snapshot->empty()) &&
      native_renderer) {
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

  MiniRtcPeerPtr* peer = nullptr;
  {
    // Audio/clipboard senders hold a shared map lock while using this pointer.
    std::unique_lock lock(remote_sessions_mutex_);
    peer = std::exchange(props->peer_, nullptr);
  }
  HostPlatform platform;
  {
    std::lock_guard lock(props->remote_version_mutex_);
    platform = props->recent_platform_;
  }
  const auto queued_at = std::chrono::steady_clock::now();
  session_cleanup_tasks_[props->remote_id_] = session_cleanup_queue_.PostTask(
      [props, peer, frame_snapshot, video_width, video_height, platform,
       thumbnail = thumbnail_, queued_at]() mutable {
        if (props->peer_events_) props->peer_events_->Deactivate();
        if (peer && !props->lan_session_) {
          LOG_INFO("Background leave connection");
          MiniRtcLeaveConnection(peer, props->remote_id_.c_str());
          MiniRtcDestroyPeer(&peer);
        }
        if (thumbnail && props->was_connected_) {
          const char* nv12 = frame_snapshot && !frame_snapshot->empty()
              ? reinterpret_cast<const char*>(frame_snapshot->data()) : nullptr;
          const auto direct = props->lan_session_;
          const Thumbnail::LanTarget target =
              direct ? Thumbnail::LanTarget{direct->target.address,
                                            direct->target.device_id,
                                            direct->target.port}
                     : Thumbnail::LanTarget{};
          const auto record_id = direct
                                     ? Thumbnail::LanRecordId(target.device_id)
                                     : props->remote_id_;
          if (thumbnail->SaveToThumbnail(
                  nv12, video_width, video_height, record_id,
                  props->remote_host_name_,
                  props->remember_password_ ? props->remote_password_ : "",
                  platform, direct ? &target : nullptr) != 0) {
            LOG_ERROR("Failed to save recent connection");
          }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - queued_at).count();
        LOG_INFO("Session cleanup completed in {} ms", elapsed);
      });
}

void GuiRuntime::CloseServerController(const std::string& remote_id) {
  if (remote_id.rfind("lan/", 0) == 0) {
    std::shared_ptr<lan::Session> session;
    {
      std::shared_lock lock(remote_sessions_mutex_);
      if (auto it = lan_controllers_.find(remote_id);
          it != lan_controllers_.end())
        session = it->second.session;
    }
    if (session && lan_) lan_->Close(session);
    return;
  }
  if (!peer_ || remote_id.empty() || controller_cleanup_tasks_.count(remote_id))
    return;
  controller_cleanup_tasks_[remote_id] = session_cleanup_queue_.PostTask(
      [peer = peer_, transmission_id = std::string(client_id_), remote_id] {
        if (PeerDisconnectPeerConnection(peer, transmission_id.c_str(),
                                     remote_id.c_str()) != 0) {
          LOG_WARN("Could not queue controller disconnect notification");
        }
      });
}

void GuiRuntime::CloseConnectionPeer() {
  if (!peer_) {
    announcements_.Reset();
    return;
  }
  {
    std::lock_guard lock(controller_trust_mutex_);
    std::erase_if(controller_trust_, [this](const auto& item) {
      return item.second.events == peer_events_.get();
    });
  }
  peer_events_->StopAccepting();
  transfers_.Cancel(peer_events_.get());
  announcements_.Reset();
  std::vector<std::string> controllers;
  {
    std::shared_lock lock(connection_status_mutex_);
    for (const auto& [id, status] : connection_status_)
      if (id.rfind("lan/", 0) != 0) controllers.push_back(id);
  }
  for (const auto& id : controllers)
    HandleServerControllerDisconnected(id, "peer_closed");
#ifdef _WIN32
  {
    std::lock_guard lock(local_host_state_mutex_);
    mirrored_controllers_.clear();
    local_controller_info_.clear();
    local_host_status_.controllers_dirty = true;
  }
#endif
  signal_connected_ = false;
  device_presence_cache_.SetSignalConnected(false);
  signal_status_ = MiniRtcSignalStatus::MiniRtcSignalClosed;
  MiniRtcPeerPtr* peer;
  {
    std::unique_lock lock(remote_sessions_mutex_);
    peer = std::exchange(peer_, nullptr);
  }
  // The same serial queue also owns outstanding controller disconnects, so
  // these finish before the shared peer is destroyed.
  connection_peer_cleanup_ = session_cleanup_queue_.PostTask(
      [peer, handler = peer_events_, id = std::string(client_id_)]() mutable {
        handler->Deactivate();
        LOG_INFO("Background connection peer cleanup");
        if (!IsLocalPeer(peer)) MiniRtcLeaveConnection(peer, id.c_str());
        UnbindLocalPeer(peer);
        MiniRtcDestroyPeer(&peer);
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
  transfers_.Poll();
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
      LOG_ERROR("Session cleanup failed");
      LOG_DEBUG("Session cleanup failed for [{}]: {}", it->first, error.what());
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
    ConnectTo(id, request.password.c_str(), request.remember_password, false,
              request.background);
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
      LOG_ERROR("Session cleanup failed");
      LOG_DEBUG("Session cleanup failed for [{}]: {}", id, error.what());
    }
  }
  session_cleanup_tasks_.clear();
}

void GuiRuntime::ResetRemoteSessionResources(
    std::shared_ptr<RemoteSession> props) {
  props->net_traffic_stats_.Reset();
  props->received_video_stats_.Reset();
  {
    std::lock_guard lock(props->gallery_frame_mutex_);
    props->gallery_video_latency_->Reset();
    props->gallery_frame_timing_ = {};
  }
  {
    std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
    props->video_latency_->Reset();
    props->video_frame_timing_ = {};
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
    if (remote_id.rfind("lan-", 0) == 0)
      for (const auto& [_, props] : remote_sessions_)
        if (props->lan_session_ && !props->closing_ &&
            Thumbnail::LanRecordId(props->lan_session_->target.device_id) ==
                remote_id)
          return props;
    return nullptr;
  }
  return it->second;
}

int GuiRuntime::ConnectTo(const std::string& remote_id, const char* password,
                          bool remember_password, bool bypass_presence_check,
                          bool background) {
  // Do not recreate a failed peer or send another probe during a server cooldown.
  if (const auto current = FindRemoteSession(remote_id);
      current && current->connection_auth_.Get().retry_after > 0) {
    if (!background) focused_remote_id_ = remote_id;
    if (!background) show_connection_status_window_ = true;
    current->rejoin_ = false;
    return 0;
  }
  if (session_cleanup_tasks_.count(remote_id)) {
    pending_reconnects_[remote_id] = {password ? password : "",
                                      remember_password, background};
    LOG_INFO("Reconnect queued until previous peer is destroyed");
    return 0;
  }
  // Wall requests, including deferred reconnects, must not replace the single
  // interactive presence probe. Join reports availability for each session.
  if (!background && !bypass_presence_check &&
      !device_presence_cache_.IsOnline(remote_id)) {
    int ret = RequestSingleDevicePresence(remote_id, password,
                                          remember_password);
    if (ret != 0) {
      offline_warning_text_ =
          localization::device_offline[localization_language_index_];
      show_offline_warning_window_ = true;
      LOG_WARN("Presence probe failed, ret={}", ret);
    } else {
      LOG_INFO("Presence probe requested before connect");
    }
    return -1;
  }

  LOG_INFO("Connecting to remote device");
  LOG_DEBUG("Connect to [{}]", remote_id);
  if (!background) focused_remote_id_ = remote_id;

  auto stale = FindRemoteSession(remote_id);
  bool exists = stale != nullptr;
  if (stale) {
    // A failed CreatePeer or a completed close can leave a session record
    // without a reusable peer. Recreate it instead of leaving the UI in
    // Connecting forever while JoinConnection has nothing to call.
    if (stale->peer_ == nullptr) {
      std::unique_lock lock(remote_sessions_mutex_);
      auto it = remote_sessions_.find(remote_id);
      if (it != remote_sessions_.end() && it->second == stale) {
        remote_sessions_.erase(it);
        exists = false;
        LOG_INFO("Recreating stale remote session before connect");
      }
    }
    if (exists && !stale->closing_.load() &&
        !stale->connection_established_) {
      const auto status = stale->connection_status_.load();
      if (status == MiniRtcConnectionStatus::MiniRtcConnectionDisconnected ||
          status == MiniRtcConnectionStatus::MiniRtcConnectionFailed ||
          status == MiniRtcConnectionStatus::MiniRtcConnectionClosed) {
        // The recreated session starts with an empty password; callers that
        // pass none expect the one stored by the previous attempt to be reused.
        const bool keep_stored_password = !password || !*password;
        pending_reconnects_[remote_id] = {
            keep_stored_password ? std::string(stale->remote_password_)
                                 : std::string(password),
            remember_password, background};
        CloseRemoteSession(stale);
        LOG_INFO("Reconnecting after terminal peer status {}",
                 static_cast<int>(status));
        return 0;
      }
    }
  }

  if (!exists) {
    MiniRtcPeerPtr* peer_to_init = nullptr;
    std::string local_id;

    {
      std::unique_lock unique_lock(remote_sessions_mutex_);
      if (remote_sessions_.find(remote_id) == remote_sessions_.end()) {
        remote_sessions_[remote_id] = std::make_shared<RemoteSession>();
        auto props = remote_sessions_[remote_id];
        props->local_id_ = "C-" + std::string(client_id_);
        props->remote_id_ = remote_id;
        props->wall_managed_ = background;
        props->tab_opened_ = !background;
        props->control_mouse_ = !background;
        props->enable_mouse_control_ = !background;
        props->audio_capture_button_pressed_ = !background;
        memcpy(&props->params_, &params_, sizeof(MiniRtcParams));
        props->params_.user_id = props->local_id_.c_str();
        props->peer_events_ = std::make_shared<PeerEventHandler>(*this);
        props->params_.user_data = props->peer_events_.get();
        props->peer_ = MiniRtcCreatePeer(&props->params_);

        props->connection_status_.store(MiniRtcConnectionStatus::MiniRtcConnectionConnecting);
        if (!background) show_connection_status_window_ = true;

        if (!props->peer_) {
          LOG_INFO("Create peer instance failed");
          return -1;
        }

        const auto& displays = devices_.display_info_list();
        for (size_t index = 0; index < displays.size(); ++index) {
          const std::string stream_id = MakeDisplayStreamId(index);
          MiniRtcAddVideoStream(props->peer_, stream_id.c_str());
        }
        MiniRtcAddAudioStream(props->peer_, props->audio_label_.c_str());
        MiniRtcAddDataStream(props->peer_, props->data_label_.c_str(), false);
        MiniRtcAddDataStream(props->peer_, props->mouse_label_.c_str(), true);
        MiniRtcAddDataStream(props->peer_, props->keyboard_label_.c_str(), true);
        MiniRtcAddDataStream(props->peer_, props->control_data_label_.c_str(), true);
        MiniRtcAddDataStream(props->peer_, props->file_label_.c_str(), true);
        MiniRtcAddDataStream(props->peer_, props->file_feedback_label_.c_str(), true);
        MiniRtcAddDataStream(props->peer_, props->clipboard_label_.c_str(), true);

        props->connection_status_.store(MiniRtcConnectionStatus::MiniRtcConnectionConnecting);

        peer_to_init = props->peer_;
        local_id = props->local_id_;
      }
    }

    if (peer_to_init) {
      LOG_INFO("Create peer instance successful");
      LOG_DEBUG("Local peer id: [{}]", local_id);
      MiniRtcInit(peer_to_init);
      LOG_INFO("Peer initialization finished");
    }
  }

  int ret = -1;
  auto props = FindRemoteSession(remote_id);
  if (!props || props->closing_) return -1;
  if (background) {
    // This also covers reusing a foreground connection that is awaiting auth.
    props->wall_managed_ = true;
    MuteRemoteAudio(props);
  }
  if (!props->connection_established_) {
    if (!background) show_connection_status_window_ = true;

    props->remember_password_ = remember_password;
    if (password && *password &&
        strcmp(password, props->remote_password_) != 0) {
      strncpy(props->remote_password_, password,
              sizeof(props->remote_password_) - 1);
      props->remote_password_[sizeof(props->remote_password_) - 1] = '\0';
    }

    // Ask locally before Join; an empty-password round trip is no longer needed.
    if (!props->connection_auth_.Begin(props->remote_password_)) {
      props->rejoin_ = false;
      return 0;
    }
    props->connection_status_.store(MiniRtcConnectionStatus::MiniRtcConnectionConnecting);
    std::string remote_id_with_pwd = remote_id + "@" + props->remote_password_;
    if (props->peer_) {
      ret = MiniRtcJoinConnection(props->peer_, remote_id_with_pwd.c_str());
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
