#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "clipboard.h"
#include "device_controller.h"
#include "file_transfer.h"
#include "localization.h"
#include "minirtc.h"
#include "platform.h"
#include "rd_log.h"
#include "render.h"

#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

namespace crossdesk {

void Render::OnSignalMessageCb(const char* message, size_t size,
                               void* user_data) {
  Render* render = (Render*)user_data;
  if (!render || !message || size == 0) {
    return;
  }
  std::string s(message, size);
  auto j = nlohmann::json::parse(s, nullptr, false);
  if (j.is_discarded() || !j.contains("type") || !j["type"].is_string()) {
    return;
  }
  std::string type = j["type"].get<std::string>();
  if (type == "presence") {
    if (j.contains("devices") && j["devices"].is_array()) {
      for (auto& dev : j["devices"]) {
        if (!dev.is_object()) {
          continue;
        }
        if (!dev.contains("id") || !dev["id"].is_string()) {
          continue;
        }
        if (!dev.contains("online") || !dev["online"].is_boolean()) {
          continue;
        }
        std::string id = dev["id"].get<std::string>();
        bool online = dev["online"].get<bool>();
        render->device_presence_.SetOnline(id, online);
      }
    }
  } else if (type == "presence_update") {
    if (j.contains("id") && j["id"].is_string() && j.contains("online") &&
        j["online"].is_boolean()) {
      std::string id = j["id"].get<std::string>();
      bool online = j["online"].get<bool>();
      if (!id.empty()) {
        render->device_presence_.SetOnline(id, online);
      }
    }
  }
}

int Render::SendKeyCommand(int key_code, bool is_down) {
  RemoteAction remote_action;
  remote_action.type = ControlType::keyboard;
  if (is_down) {
    remote_action.k.flag = KeyFlag::key_down;
  } else {
    remote_action.k.flag = KeyFlag::key_up;
  }
  remote_action.k.key_value = key_code;

  std::string target_id = controlled_remote_id_.empty() ? focused_remote_id_
                                                        : controlled_remote_id_;
  if (!target_id.empty()) {
    if (client_properties_.find(target_id) != client_properties_.end()) {
      auto props = client_properties_[target_id];
      if (props->connection_status_ == ConnectionStatus::Connected &&
          props->peer_) {
        std::string msg = remote_action.to_json();
        SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                      props->data_label_.c_str());
      }
    }
  }

  return 0;
}

int Render::ProcessMouseEvent(const SDL_Event& event) {
  controlled_remote_id_ = "";
  int video_width, video_height = 0;
  int render_width, render_height = 0;
  float ratio_x, ratio_y = 0;
  RemoteAction remote_action;

  // std::shared_lock lock(client_properties_mutex_);
  for (auto& it : client_properties_) {
    auto props = it.second;
    if (!props->control_mouse_) {
      continue;
    }

    if (event.button.x >= props->stream_render_rect_.x &&
        event.button.x <=
            props->stream_render_rect_.x + props->stream_render_rect_.w &&
        event.button.y >= props->stream_render_rect_.y &&
        event.button.y <=
            props->stream_render_rect_.y + props->stream_render_rect_.h) {
      controlled_remote_id_ = it.first;
      render_width = props->stream_render_rect_.w;
      render_height = props->stream_render_rect_.h;
      last_mouse_event.button.x = event.button.x;
      last_mouse_event.button.y = event.button.y;

      remote_action.m.x =
          (float)(event.button.x - props->stream_render_rect_.x) / render_width;
      remote_action.m.y =
          (float)(event.button.y - props->stream_render_rect_.y) /
          render_height;

      if (SDL_EVENT_MOUSE_BUTTON_DOWN == event.type) {
        remote_action.type = ControlType::mouse;
        if (SDL_BUTTON_LEFT == event.button.button) {
          remote_action.m.flag = MouseFlag::left_down;
        } else if (SDL_BUTTON_RIGHT == event.button.button) {
          remote_action.m.flag = MouseFlag::right_down;
        } else if (SDL_BUTTON_MIDDLE == event.button.button) {
          remote_action.m.flag = MouseFlag::middle_down;
        }
      } else if (SDL_EVENT_MOUSE_BUTTON_UP == event.type) {
        remote_action.type = ControlType::mouse;
        if (SDL_BUTTON_LEFT == event.button.button) {
          remote_action.m.flag = MouseFlag::left_up;
        } else if (SDL_BUTTON_RIGHT == event.button.button) {
          remote_action.m.flag = MouseFlag::right_up;
        } else if (SDL_BUTTON_MIDDLE == event.button.button) {
          remote_action.m.flag = MouseFlag::middle_up;
        }
      } else if (SDL_EVENT_MOUSE_MOTION == event.type) {
        remote_action.type = ControlType::mouse;
        remote_action.m.flag = MouseFlag::move;
      }

      if (props->control_bar_hovered_ || props->display_selectable_hovered_) {
        break;
      }
      if (props->peer_) {
        std::string msg = remote_action.to_json();
        SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                      props->data_label_.c_str());
      }
    } else if (SDL_EVENT_MOUSE_WHEEL == event.type &&
               last_mouse_event.button.x >= props->stream_render_rect_.x &&
               last_mouse_event.button.x <= props->stream_render_rect_.x +
                                                props->stream_render_rect_.w &&
               last_mouse_event.button.y >= props->stream_render_rect_.y &&
               last_mouse_event.button.y <= props->stream_render_rect_.y +
                                                props->stream_render_rect_.h) {
      float scroll_x = event.wheel.x;
      float scroll_y = event.wheel.y;
      if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        scroll_x = -scroll_x;
        scroll_y = -scroll_y;
      }

      remote_action.type = ControlType::mouse;

      auto roundUp = [](float value) -> int {
        if (value > 0) {
          return static_cast<int>(std::ceil(value));
        } else if (value < 0) {
          return static_cast<int>(std::floor(value));
        }
        return 0;
      };

      if (std::abs(scroll_y) >= std::abs(scroll_x)) {
        remote_action.m.flag = MouseFlag::wheel_vertical;
        remote_action.m.s = roundUp(scroll_y);
      } else {
        remote_action.m.flag = MouseFlag::wheel_horizontal;
        remote_action.m.s = roundUp(scroll_x);
      }

      render_width = props->stream_render_rect_.w;
      render_height = props->stream_render_rect_.h;
      remote_action.m.x =
          (float)(last_mouse_event.button.x - props->stream_render_rect_.x) /
          render_width;
      remote_action.m.y =
          (float)(last_mouse_event.button.y - props->stream_render_rect_.y) /
          render_height;

      if (props->control_bar_hovered_) {
        continue;
      }
      if (props->peer_) {
        std::string msg = remote_action.to_json();
        SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                      props->data_label_.c_str());
      }
    }
  }

  return 0;
}

void Render::SdlCaptureAudioIn(void* userdata, Uint8* stream, int len) {
  Render* render = (Render*)userdata;
  if (!render) {
    return;
  }

  if (1) {
    // std::shared_lock lock(render->client_properties_mutex_);
    for (const auto& it : render->client_properties_) {
      auto props = it.second;
      if (props->connection_status_ == ConnectionStatus::Connected) {
        if (props->peer_) {
          SendAudioFrame(props->peer_, (const char*)stream, len,
                         render->audio_label_.c_str());
        }
      }
    }

  } else {
    memcpy(render->audio_buffer_, stream, len);
    render->audio_len_ = len;
    SDL_Delay(10);
    render->audio_buffer_fresh_ = true;
  }
}

void Render::SdlCaptureAudioOut([[maybe_unused]] void* userdata,
                                [[maybe_unused]] Uint8* stream,
                                [[maybe_unused]] int len) {
  // Render *render = (Render *)userdata;
  // for (auto it : render->client_properties_) {
  //   auto props = it.second;
  //   if (props->connection_status_ == SignalStatus::SignalConnected) {
  //     SendAudioFrame(props->peer_, (const char *)stream, len);
  //   }
  // }

  // if (!render->audio_buffer_fresh_) {
  //   return;
  // }

  // SDL_memset(stream, 0, len);

  // if (render->audio_len_ == 0) {
  //   return;
  // } else {
  // }

  // len = (len > render->audio_len_ ? render->audio_len_ : len);
  // SDL_MixAudioFormat(stream, render->audio_buffer_, AUDIO_S16LSB, len,
  //                    SDL_MIX_MAXVOLUME);
  // render->audio_buffer_fresh_ = false;
}

void Render::OnReceiveVideoBufferCb(const XVideoFrame* video_frame,
                                    const char* user_id, size_t user_id_size,
                                    const char* src_id, size_t src_id_size,
                                    void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(render->client_properties_mutex_);
  if (render->client_properties_.find(remote_id) ==
      render->client_properties_.end()) {
    return;
  }
  SubStreamWindowProperties* props =
      render->client_properties_.find(remote_id)->second.get();

  if (props->connection_established_) {
    {
      std::lock_guard<std::mutex> lock(props->video_frame_mutex_);

      if (!props->back_frame_) {
        props->back_frame_ =
            std::make_shared<std::vector<unsigned char>>(video_frame->size);
      }
      if (props->back_frame_->size() != video_frame->size) {
        props->back_frame_->resize(video_frame->size);
      }

      std::memcpy(props->back_frame_->data(), video_frame->data,
                  video_frame->size);

      const bool size_changed = (props->video_width_ != video_frame->width) ||
                                (props->video_height_ != video_frame->height);
      if (size_changed) {
        props->render_rect_dirty_ = true;
      }

      props->video_width_ = video_frame->width;
      props->video_height_ = video_frame->height;
      props->video_size_ = video_frame->size;

      props->front_frame_.swap(props->back_frame_);
    }

    SDL_Event event;
    event.type = render->STREAM_REFRESH_EVENT;
    event.user.data1 = props;
    SDL_PushEvent(&event);
    props->streaming_ = true;

    if (props->net_traffic_stats_button_pressed_) {
      props->frame_count_++;
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - props->last_time_)
                         .count();

      if (elapsed >= 1000) {
        props->fps_ = props->frame_count_ * 1000 / elapsed;
        props->frame_count_ = 0;
        props->last_time_ = now;
      }
    }
  }
}

void Render::OnReceiveAudioBufferCb(const char* data, size_t size,
                                    const char* user_id, size_t user_id_size,
                                    const char* src_id, size_t src_id_size,
                                    void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  render->audio_buffer_fresh_ = true;

  if (render->output_stream_) {
    int pushed = SDL_PutAudioStreamData(
        render->output_stream_, (const Uint8*)data, static_cast<int>(size));
    if (pushed < 0) {
      LOG_ERROR("Failed to push audio data: {}", SDL_GetError());
    }
  }
}

void Render::OnReceiveDataBufferCb(const char* data, size_t size,
                                   const char* user_id, size_t user_id_size,
                                   const char* src_id, size_t src_id_size,
                                   void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  std::string source_id = std::string(src_id, src_id_size);
  if (source_id == render->file_label_) {
    std::string remote_user_id = std::string(user_id, user_id_size);

    static FileReceiver receiver;
    // Update output directory from config
    std::string configured_path =
        render->config_center_->GetFileTransferSavePath();
    if (!configured_path.empty()) {
      receiver.SetOutputDir(std::filesystem::u8path(configured_path));
    } else if (receiver.OutputDir().empty()) {
      receiver = FileReceiver();  // re-init with default desktop path
    }
    receiver.SetOnSendAck([render,
                           remote_user_id](const FileTransferAck& ack) -> int {
      bool is_server_sending = remote_user_id.rfind("C-", 0) != 0;
      if (is_server_sending) {
        auto props =
            render->GetSubStreamWindowPropertiesByRemoteId(remote_user_id);
        if (props) {
          PeerPtr* peer = props->peer_;
          return SendReliableDataFrame(
              peer, reinterpret_cast<const char*>(&ack),
              sizeof(FileTransferAck), render->file_feedback_label_.c_str());
        }
      }

      return SendReliableDataFrame(
          render->peer_, reinterpret_cast<const char*>(&ack),
          sizeof(FileTransferAck), render->file_feedback_label_.c_str());
    });

    receiver.OnData(data, size);
    return;
  } else if (source_id == render->clipboard_label_) {
    if (size > 0) {
      std::string remote_user_id(user_id, user_id_size);
      auto props =
          render->GetSubStreamWindowPropertiesByRemoteId(remote_user_id);
      if (props && !props->enable_mouse_control_) {
        return;
      }

      std::string clipboard_text(data, size);
      if (!Clipboard::SetText(clipboard_text)) {
        LOG_ERROR("Failed to set clipboard content from remote");
      }
    }
    return;
  } else if (source_id == render->file_feedback_label_) {
    if (size < sizeof(FileTransferAck)) {
      LOG_ERROR("FileTransferAck: buffer too small, size={}", size);
      return;
    }

    FileTransferAck ack{};
    memcpy(&ack, data, sizeof(FileTransferAck));

    if (ack.magic != kFileAckMagic) {
      LOG_ERROR(
          "FileTransferAck: invalid magic, got 0x{:08X}, expected 0x{:08X}",
          ack.magic, kFileAckMagic);
      return;
    }

    std::shared_ptr<SubStreamWindowProperties> props = nullptr;
    {
      std::shared_lock lock(render->file_id_to_props_mutex_);
      auto it = render->file_id_to_props_.find(ack.file_id);
      if (it != render->file_id_to_props_.end()) {
        props = it->second.lock();
      }
    }

    Render::FileTransferState* state = nullptr;
    if (!props) {
      {
        std::shared_lock lock(render->file_id_to_transfer_state_mutex_);
        auto it = render->file_id_to_transfer_state_.find(ack.file_id);
        if (it != render->file_id_to_transfer_state_.end()) {
          state = it->second;
        }
      }

      if (!state) {
        LOG_WARN("FileTransferAck: no props/state found for file_id={}",
                 ack.file_id);
        return;
      }
    } else {
      state = &props->file_transfer_;
    }

    // Update progress based on ACK
    state->file_sent_bytes_ = ack.acked_offset;
    state->file_total_bytes_ = ack.total_size;

    uint32_t rate_bps = 0;
    {
      if (props) {
        uint32_t data_channel_bitrate =
            props->net_traffic_stats_.data_outbound_stats.bitrate;

        if (data_channel_bitrate > 0 && state->file_sending_.load()) {
          rate_bps = static_cast<uint32_t>(data_channel_bitrate * 0.99f);

          uint32_t current_rate = state->file_send_rate_bps_.load();
          if (current_rate > 0) {
            // 70% old + 30% new for smoother display
            rate_bps =
                static_cast<uint32_t>(current_rate * 0.7 + rate_bps * 0.3);
          }
        } else {
          rate_bps = state->file_send_rate_bps_.load();
        }
      } else {
        // Global transfer: no per-connection bitrate available.
        // Estimate send rate from ACKed bytes delta over time.
        const uint32_t current_rate = state->file_send_rate_bps_.load();
        uint32_t estimated_rate_bps = 0;
        const auto now = std::chrono::steady_clock::now();

        uint64_t last_bytes = 0;
        std::chrono::steady_clock::time_point last_time;
        {
          std::lock_guard<std::mutex> lock(state->file_transfer_mutex_);
          last_bytes = state->file_send_last_bytes_;
          last_time = state->file_send_last_update_time_;
        }

        if (state->file_sending_.load() && ack.acked_offset >= last_bytes) {
          const uint64_t delta_bytes = ack.acked_offset - last_bytes;
          const double delta_seconds =
              std::chrono::duration<double>(now - last_time).count();

          if (delta_seconds > 0.0 && delta_bytes > 0) {
            const double bps =
                (static_cast<double>(delta_bytes) * 8.0) / delta_seconds;
            if (bps > 0.0) {
              const double capped =
                  (std::min)(bps, static_cast<double>(
                                      (std::numeric_limits<uint32_t>::max)()));
              estimated_rate_bps = static_cast<uint32_t>(capped);
            }
          }
        }

        if (estimated_rate_bps > 0 && current_rate > 0) {
          // 70% old + 30% new for smoother display
          rate_bps = static_cast<uint32_t>(current_rate * 0.7 +
                                           estimated_rate_bps * 0.3);
        } else if (estimated_rate_bps > 0) {
          rate_bps = estimated_rate_bps;
        } else {
          rate_bps = current_rate;
        }
      }

      state->file_send_rate_bps_ = rate_bps;
      state->file_send_last_bytes_ = ack.acked_offset;
      auto now = std::chrono::steady_clock::now();
      state->file_send_last_update_time_ = now;
    }

    // Update file transfer list: update progress and rate
    {
      std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
      for (auto& info : state->file_transfer_list_) {
        if (info.file_id == ack.file_id) {
          info.sent_bytes = ack.acked_offset;
          info.file_size = ack.total_size;
          info.rate_bps = rate_bps;
          break;
        }
      }
    }

    // Check if transfer is completed
    if ((ack.flags & 0x01) != 0) {
      // Transfer completed - receiver has finished receiving the file
      // Reopen window if it was closed by user
      state->file_transfer_window_visible_ = true;
      state->file_sending_ = false;  // Mark sending as finished
      LOG_INFO(
          "File transfer completed via ACK, file_id={}, total_size={}, "
          "acked_offset={}",
          ack.file_id, ack.total_size, ack.acked_offset);

      // Update file transfer list: mark as completed
      {
        std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
        for (auto& info : state->file_transfer_list_) {
          if (info.file_id == ack.file_id) {
            info.status =
                Render::FileTransferState::FileTransferStatus::Completed;
            info.sent_bytes = ack.total_size;
            break;
          }
        }
      }

      // Unregister file_id mapping after completion
      {
        if (props) {
          std::lock_guard<std::shared_mutex> lock(
              render->file_id_to_props_mutex_);
          render->file_id_to_props_.erase(ack.file_id);
        } else {
          std::lock_guard<std::shared_mutex> lock(
              render->file_id_to_transfer_state_mutex_);
          render->file_id_to_transfer_state_.erase(ack.file_id);
        }
      }

      // Process next file in queue
      render->ProcessFileQueue(props);
    }

    return;
  }

  std::string json_str(data, size);
  RemoteAction remote_action;

  try {
    remote_action.from_json(json_str);
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to parse RemoteAction JSON: {}", e.what());
    return;
  }

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(render->client_properties_mutex_);
  if (remote_action.type == ControlType::host_infomation) {
    if (render->client_properties_.find(remote_id) !=
        render->client_properties_.end()) {
      // client mode
      auto props = render->client_properties_.find(remote_id)->second;
      if (props && props->remote_host_name_.empty()) {
        props->remote_host_name_ = std::string(remote_action.i.host_name,
                                               remote_action.i.host_name_size);
        LOG_INFO("Remote hostname: [{}]", props->remote_host_name_);

        for (int i = 0; i < remote_action.i.display_num; i++) {
          props->display_info_list_.push_back(
              DisplayInfo(remote_action.i.display_list[i],
                          remote_action.i.left[i], remote_action.i.top[i],
                          remote_action.i.right[i], remote_action.i.bottom[i]));
        }
      }
      FreeRemoteAction(remote_action);
    } else {
      // server mode
      render->connection_host_names_[remote_id] = std::string(
          remote_action.i.host_name, remote_action.i.host_name_size);
      LOG_INFO("Remote hostname: [{}]",
               render->connection_host_names_[remote_id]);
      FreeRemoteAction(remote_action);
    }
  } else {
    // remote
    if (remote_action.type == ControlType::mouse && render->mouse_controller_) {
      render->mouse_controller_->SendMouseCommand(remote_action,
                                                  render->selected_display_);
    } else if (remote_action.type == ControlType::audio_capture) {
      if (remote_action.a && !render->start_speaker_capturer_)
        render->StartSpeakerCapturer();
      else if (!remote_action.a && render->start_speaker_capturer_)
        render->StopSpeakerCapturer();
    } else if (remote_action.type == ControlType::keyboard &&
               render->keyboard_capturer_) {
      render->keyboard_capturer_->SendKeyboardCommand(
          (int)remote_action.k.key_value,
          remote_action.k.flag == KeyFlag::key_down);
    } else if (remote_action.type == ControlType::display_id &&
               render->screen_capturer_) {
      render->selected_display_ = remote_action.d;
      render->screen_capturer_->SwitchTo(remote_action.d);
    }
  }
}

void Render::OnSignalStatusCb(SignalStatus status, const char* user_id,
                              size_t user_id_size, void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  std::string client_id(user_id, user_id_size);
  if (client_id == render->client_id_) {
    render->signal_status_ = status;
    if (SignalStatus::SignalConnecting == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalConnected == status) {
      render->signal_connected_ = true;
      LOG_INFO("[{}] connected to signal server", client_id);
    } else if (SignalStatus::SignalFailed == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalClosed == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalReconnecting == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalServerClosed == status) {
      render->signal_connected_ = false;
    }
  } else {
    if (client_id.rfind("C-", 0) != 0) {
      return;
    }

    std::string remote_id(client_id.begin() + 2, client_id.end());
    // std::shared_lock lock(render->client_properties_mutex_);
    if (render->client_properties_.find(remote_id) ==
        render->client_properties_.end()) {
      return;
    }
    auto props = render->client_properties_.find(remote_id)->second;
    props->signal_status_ = status;
    if (SignalStatus::SignalConnecting == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalConnected == status) {
      props->signal_connected_ = true;
      LOG_INFO("[{}] connected to signal server", remote_id);
    } else if (SignalStatus::SignalFailed == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalClosed == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalReconnecting == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalServerClosed == status) {
      props->signal_connected_ = false;
    }
  }
}

void Render::OnConnectionStatusCb(ConnectionStatus status, const char* user_id,
                                  const size_t user_id_size, void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) return;

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(render->client_properties_mutex_);
  auto it = render->client_properties_.find(remote_id);
  auto props = (it != render->client_properties_.end()) ? it->second : nullptr;

  if (props) {
    render->is_client_mode_ = true;
    render->show_connection_status_window_ = true;
    props->connection_status_ = status;

    switch (status) {
      case ConnectionStatus::Connected: {
        {
          RemoteAction remote_action;
          remote_action.i.display_num = render->display_info_list_.size();
          remote_action.i.display_list =
              (char**)malloc(remote_action.i.display_num * sizeof(char*));
          remote_action.i.left =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.top =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.right =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.bottom =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          for (int i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     render->display_info_list_[i].name);
            remote_action.i.display_list[i] =
                (char*)malloc(render->display_info_list_[i].name.length() + 1);
            strncpy(remote_action.i.display_list[i],
                    render->display_info_list_[i].name.c_str(),
                    render->display_info_list_[i].name.length());
            remote_action.i
                .display_list[i][render->display_info_list_[i].name.length()] =
                '\0';
            remote_action.i.left[i] = render->display_info_list_[i].left;
            remote_action.i.top[i] = render->display_info_list_[i].top;
            remote_action.i.right[i] = render->display_info_list_[i].right;
            remote_action.i.bottom[i] = render->display_info_list_[i].bottom;
          }

          std::string host_name = GetHostName();
          remote_action.type = ControlType::host_infomation;
          memcpy(&remote_action.i.host_name, host_name.data(),
                 host_name.size());
          remote_action.i.host_name[host_name.size()] = '\0';
          remote_action.i.host_name_size = host_name.size();

          std::string msg = remote_action.to_json();
          int ret = SendReliableDataFrame(props->peer_, msg.data(), msg.size(),
                                          render->control_data_label_.c_str());
          FreeRemoteAction(remote_action);
        }

        if (!render->need_to_create_stream_window_ &&
            !render->client_properties_.empty()) {
          render->need_to_create_stream_window_ = true;
        }
        props->connection_established_ = true;
        props->stream_render_rect_ = {
            0, (int)render->title_bar_height_,
            (int)render->stream_window_width_,
            (int)(render->stream_window_height_ - render->title_bar_height_)};
        render->start_keyboard_capturer_ = true;
        break;
      }
      case ConnectionStatus::Disconnected:
      case ConnectionStatus::Failed:
      case ConnectionStatus::Closed: {
        props->connection_established_ = false;
        props->enable_mouse_control_ = false;

        {
          std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
          props->front_frame_.reset();
          props->back_frame_.reset();
          props->video_width_ = 0;
          props->video_height_ = 0;
          props->video_size_ = 0;
          props->render_rect_dirty_ = true;
          props->stream_cleanup_pending_ = true;
        }

        SDL_Event event;
        event.type = render->STREAM_REFRESH_EVENT;
        event.user.data1 = props.get();
        SDL_PushEvent(&event);

        render->focus_on_stream_window_ = false;

        break;
      }
      case ConnectionStatus::IncorrectPassword: {
        render->password_validating_ = false;
        render->password_validating_time_++;
        if (render->connect_button_pressed_) {
          render->connect_button_pressed_ = false;
          props->connection_established_ = false;
          render->connect_button_label_ =
              localization::connect[render->localization_language_index_];
        }
        break;
      }
      case ConnectionStatus::NoSuchTransmissionId: {
        if (render->connect_button_pressed_) {
          props->connection_established_ = false;
          render->connect_button_label_ =
              localization::connect[render->localization_language_index_];
        }
        break;
      }
      default:
        break;
    }
  } else {
    render->is_client_mode_ = false;
    render->show_connection_status_window_ = true;
    render->connection_status_[remote_id] = status;

    switch (status) {
      case ConnectionStatus::Connected: {
        {
          RemoteAction remote_action;
          remote_action.i.display_num = render->display_info_list_.size();
          remote_action.i.display_list =
              (char**)malloc(remote_action.i.display_num * sizeof(char*));
          remote_action.i.left =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.top =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.right =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.bottom =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          for (int i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     render->display_info_list_[i].name);
            remote_action.i.display_list[i] =
                (char*)malloc(render->display_info_list_[i].name.length() + 1);
            strncpy(remote_action.i.display_list[i],
                    render->display_info_list_[i].name.c_str(),
                    render->display_info_list_[i].name.length());
            remote_action.i
                .display_list[i][render->display_info_list_[i].name.length()] =
                '\0';
            remote_action.i.left[i] = render->display_info_list_[i].left;
            remote_action.i.top[i] = render->display_info_list_[i].top;
            remote_action.i.right[i] = render->display_info_list_[i].right;
            remote_action.i.bottom[i] = render->display_info_list_[i].bottom;
          }

          std::string host_name = GetHostName();
          remote_action.type = ControlType::host_infomation;
          memcpy(&remote_action.i.host_name, host_name.data(),
                 host_name.size());
          remote_action.i.host_name[host_name.size()] = '\0';
          remote_action.i.host_name_size = host_name.size();

          std::string msg = remote_action.to_json();
          int ret = SendReliableDataFrame(render->peer_, msg.data(), msg.size(),
                                          render->control_data_label_.c_str());
          FreeRemoteAction(remote_action);
        }

        render->need_to_create_server_window_ = true;
        render->is_server_mode_ = true;
        render->start_screen_capturer_ = true;
        render->start_speaker_capturer_ = true;
        render->remote_client_id_ = remote_id;
        render->start_mouse_controller_ = true;
        if (std::all_of(render->connection_status_.begin(),
                        render->connection_status_.end(), [](const auto& kv) {
                          return kv.first.find("web") != std::string::npos;
                        })) {
          render->show_cursor_ = true;
        }

        break;
      }
      case ConnectionStatus::Closed: {
        if (std::all_of(render->connection_status_.begin(),
                        render->connection_status_.end(), [](const auto& kv) {
                          return kv.second == ConnectionStatus::Closed ||
                                 kv.second == ConnectionStatus::Failed ||
                                 kv.second == ConnectionStatus::Disconnected;
                        })) {
          render->need_to_destroy_server_window_ = true;
          render->is_server_mode_ = false;
          render->start_screen_capturer_ = false;
          render->start_speaker_capturer_ = false;
          render->start_mouse_controller_ = false;
          render->start_keyboard_capturer_ = false;
          render->remote_client_id_ = "";
          if (props) props->connection_established_ = false;
          if (render->audio_capture_) {
            render->StopSpeakerCapturer();
            render->audio_capture_ = false;
          }

          render->connection_status_.erase(remote_id);
          if (render->screen_capturer_) {
            render->screen_capturer_->ResetToInitialMonitor();
          }
        }

        if (std::all_of(render->connection_status_.begin(),
                        render->connection_status_.end(), [](const auto& kv) {
                          return kv.first.find("web") == std::string::npos;
                        })) {
          render->show_cursor_ = false;
        }

        break;
      }
      default:
        break;
    }
  }
}

void Render::OnNetStatusReport(const char* client_id, size_t client_id_size,
                               TraversalMode mode,
                               const XNetTrafficStats* net_traffic_stats,
                               const char* user_id, const size_t user_id_size,
                               void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  if (strchr(client_id, '@') != nullptr && strchr(user_id, '-') == nullptr) {
    std::string id, password;
    const char* at_pos = strchr(client_id, '@');
    if (at_pos == nullptr) {
      id = client_id;
      password.clear();
    } else {
      id.assign(client_id, at_pos - client_id);
      password = at_pos + 1;
    }

    bool is_self_hosted = render->config_center_->IsSelfHosted();

    if (is_self_hosted) {
      memset(&render->client_id_, 0, sizeof(render->client_id_));
      strncpy(render->client_id_, id.c_str(), sizeof(render->client_id_) - 1);
      render->client_id_[sizeof(render->client_id_) - 1] = '\0';

      memset(&render->password_saved_, 0, sizeof(render->password_saved_));
      strncpy(render->password_saved_, password.c_str(),
              sizeof(render->password_saved_) - 1);
      render->password_saved_[sizeof(render->password_saved_) - 1] = '\0';

      memset(&render->self_hosted_id_, 0, sizeof(render->self_hosted_id_));
      strncpy(render->self_hosted_id_, client_id,
              sizeof(render->self_hosted_id_) - 1);
      render->self_hosted_id_[sizeof(render->self_hosted_id_) - 1] = '\0';

      LOG_INFO("Use self-hosted client id [{}] and save to cache file", id);

      render->cd_cache_mutex_.lock();

      std::ifstream v2_file_read(render->cache_path_ + "/secure_cache_v2.enc",
                                 std::ios::binary);
      if (v2_file_read.good()) {
        v2_file_read.read(reinterpret_cast<char*>(&render->cd_cache_v2_),
                          sizeof(CDCacheV2));
        v2_file_read.close();
      } else {
        memset(&render->cd_cache_v2_, 0, sizeof(CDCacheV2));
        memset(&render->cd_cache_v2_.client_id_with_password, 0,
               sizeof(render->cd_cache_v2_.client_id_with_password));
        strncpy(render->cd_cache_v2_.client_id_with_password,
                render->client_id_with_password_,
                sizeof(render->cd_cache_v2_.client_id_with_password));
        memcpy(&render->cd_cache_v2_.key, &render->aes128_key_,
               sizeof(render->cd_cache_v2_.key));
        memcpy(&render->cd_cache_v2_.iv, &render->aes128_iv_,
               sizeof(render->cd_cache_v2_.iv));
      }

      memset(&render->cd_cache_v2_.self_hosted_id, 0,
             sizeof(render->cd_cache_v2_.self_hosted_id));
      strncpy(render->cd_cache_v2_.self_hosted_id, client_id,
              sizeof(render->cd_cache_v2_.self_hosted_id) - 1);
      render->cd_cache_v2_
          .self_hosted_id[sizeof(render->cd_cache_v2_.self_hosted_id) - 1] =
          '\0';

      memset(&render->cd_cache_v2_.client_id_with_password, 0,
             sizeof(render->cd_cache_v2_.client_id_with_password));
      strncpy(render->cd_cache_v2_.client_id_with_password,
              render->client_id_with_password_,
              sizeof(render->cd_cache_v2_.client_id_with_password));
      memcpy(&render->cd_cache_v2_.key, &render->aes128_key_,
             sizeof(render->cd_cache_v2_.key));
      memcpy(&render->cd_cache_v2_.iv, &render->aes128_iv_,
             sizeof(render->cd_cache_v2_.iv));
      std::ofstream cd_cache_v2_file(
          render->cache_path_ + "/secure_cache_v2.enc", std::ios::binary);
      if (cd_cache_v2_file) {
        cd_cache_v2_file.write(reinterpret_cast<char*>(&render->cd_cache_v2_),
                               sizeof(CDCacheV2));
        cd_cache_v2_file.close();
      }

      render->cd_cache_mutex_.unlock();
    } else {
      memset(&render->client_id_, 0, sizeof(render->client_id_));
      strncpy(render->client_id_, id.c_str(), sizeof(render->client_id_) - 1);
      render->client_id_[sizeof(render->client_id_) - 1] = '\0';

      memset(&render->password_saved_, 0, sizeof(render->password_saved_));
      strncpy(render->password_saved_, password.c_str(),
              sizeof(render->password_saved_) - 1);
      render->password_saved_[sizeof(render->password_saved_) - 1] = '\0';

      memset(&render->client_id_with_password_, 0,
             sizeof(render->client_id_with_password_));
      strncpy(render->client_id_with_password_, client_id,
              sizeof(render->client_id_with_password_) - 1);
      render
          ->client_id_with_password_[sizeof(render->client_id_with_password_) -
                                     1] = '\0';

      LOG_INFO("Use client id [{}] and save id into cache file", id);
      render->SaveSettingsIntoCacheFile();
    }
  }

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(render->client_properties_mutex_);
  if (render->client_properties_.find(remote_id) ==
      render->client_properties_.end()) {
    return;
  }
  auto props = render->client_properties_.find(remote_id)->second;
  if (props->traversal_mode_ != mode) {
    props->traversal_mode_ = mode;
    LOG_INFO("Net mode: [{}]", int(props->traversal_mode_));
  }

  if (!net_traffic_stats) {
    return;
  }

  // only display client side net status if connected to itself
  if (!(render->peer_reserved_ && !strstr(client_id, "C-"))) {
    props->net_traffic_stats_ = *net_traffic_stats;
  }
}
}  // namespace crossdesk