#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "display_stream_id.h"
#include "localization.h"
#include "platform.h"
#include "platform/video_renderer.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

namespace {
constexpr auto kPresenceProbeTimeout = std::chrono::seconds(5);
}  // namespace

void GuiRuntime::HandleConnectionStatusChange() {
  if (signal_connected_ && peer_ && need_to_send_recent_connections_) {
    if (!recent_connection_ids_.empty()) {
      nlohmann::json j;
      j["type"] = "recent_connections_presence";
      j["user_id"] = client_id_;
      j["devices"] = nlohmann::json::array();
      for (const auto& id : recent_connection_ids_) {
        std::string pure_id = id;
        size_t pos_y = pure_id.find('Y');
        size_t pos_n = pure_id.find('N');
        size_t pos = std::string::npos;
        if (pos_y != std::string::npos &&
            (pos_n == std::string::npos || pos_y < pos_n)) {
          pos = pos_y;
        } else if (pos_n != std::string::npos) {
          pos = pos_n;
        }
        if (pos != std::string::npos) {
          pure_id = pure_id.substr(0, pos);
        }
        j["devices"].push_back(pure_id);
      }
      auto s = j.dump();
      SendSignalMessage(peer_, s.data(), s.size());
    }
  }
  need_to_send_recent_connections_ = false;
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

  bool has_connected_controller = false;
  bool has_web_controller = false;
  std::string remaining_controller_id;
  {
    std::unique_lock lock(connection_status_mutex_);
    connection_status_.erase(remote_id);
    connection_host_names_.erase(remote_id);
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
  start_speaker_capturer_ = false;
  start_mouse_controller_ = false;
  start_keyboard_capturer_ = false;
  remote_client_id_.clear();
  if (audio_capture_) {
    devices_.StopSpeakerCapturer();
    audio_capture_ = false;
  }
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

  nlohmann::json j;
  j["type"] = "recent_connections_presence";
  j["user_id"] = client_id_;
  j["devices"] = nlohmann::json::array({remote_id});
  auto s = j.dump();

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

  if (frame_snapshot && !frame_snapshot->empty() && video_width > 0 &&
      video_height > 0) {
    std::vector<unsigned char> buffer_copy(*frame_snapshot);
    std::string remote_id = props->remote_id_;
    std::string remote_host_name = props->remote_host_name_;
    std::string password =
        props->remember_password_ ? props->remote_password_ : "";

    std::thread save_thread([buffer_copy, video_width, video_height, remote_id,
                             remote_host_name, password,
                             thumbnail = thumbnail_]() {
      thumbnail->SaveToThumbnail((char*)buffer_copy.data(), video_width,
                                 video_height, remote_id, remote_host_name,
                                 password);
    });

    {
      std::lock_guard<std::mutex> lock(thumbnail_save_threads_mutex_);
      thumbnail_save_threads_.emplace_back(std::move(save_thread));
    }
  }

  if (native_renderer) {
    native_renderer->DiscardStream(props->remote_id_);
    video_frame_dirty_.store(true, std::memory_order_release);
  }

  if (props->peer_) {
    LOG_INFO("[{}] Leave connection [{}]", props->local_id_, props->remote_id_);
    LeaveConnection(props->peer_, props->remote_id_.c_str());
    LOG_INFO("Destroy peer [{}]", props->local_id_);
    DestroyPeer(&props->peer_);
  }
}

void GuiRuntime::CloseAllRemoteSessions() {
  if (peer_) {
    LOG_INFO("[{}] Leave connection [{}]", client_id_, client_id_);
    LeaveConnection(peer_, client_id_);
    is_client_mode_ = false;
    devices_.StopMouseController();
    devices_.StopScreenCapturer();
    devices_.StopSpeakerCapturer();
    devices_.StopKeyboardCapturer();
    LOG_INFO("Destroy peer [{}]", client_id_);
    DestroyPeer(&peer_);
  }

  {
    // std::shared_lock lock(remote_sessions_mutex_);
    for (auto& it : remote_sessions_) {
      auto props = it.second;
      CloseRemoteSession(props);
    }
  }

  {
    // std::unique_lock lock(remote_sessions_mutex_);
    remote_sessions_.clear();
  }
}

void GuiRuntime::WaitForThumbnailSaveTasks() {
  std::vector<std::thread> threads_to_join;

  {
    std::lock_guard<std::mutex> lock(thumbnail_save_threads_mutex_);
    threads_to_join.swap(thumbnail_save_threads_);
  }

  if (threads_to_join.empty()) {
    return;
  }

  for (auto& thread : threads_to_join) {
    if (thread.joinable()) {
      thread.join();
    }
  }
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

  // std::shared_lock shared_lock(remote_sessions_mutex_);
  bool exists = (remote_sessions_.find(remote_id) != remote_sessions_.end());
  // shared_lock.unlock();

  if (!exists) {
    PeerPtr* peer_to_init = nullptr;
    std::string local_id;

    {
      // std::unique_lock unique_lock(remote_sessions_mutex_);
      if (remote_sessions_.find(remote_id) == remote_sessions_.end()) {
        remote_sessions_[remote_id] = std::make_shared<RemoteSession>();
        auto props = remote_sessions_[remote_id];
        props->local_id_ = "C-" + std::string(client_id_);
        props->remote_id_ = remote_id;
        memcpy(&props->params_, &params_, sizeof(Params));
        props->params_.user_id = props->local_id_.c_str();
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
  // std::shared_lock read_lock(remote_sessions_mutex_);
  auto props = remote_sessions_[remote_id];
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
  // read_lock.unlock();

  return 0;
}
}  // namespace crossdesk
