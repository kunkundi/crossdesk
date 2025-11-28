#include <cmath>

#include "device_controller.h"
#include "localization.h"
#include "platform.h"
#include "rd_log.h"
#include "render.h"

#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

namespace crossdesk {

int Render::SendKeyCommand(int key_code, bool is_down) {
  RemoteAction remote_action;
  remote_action.type = ControlType::keyboard;
  if (is_down) {
    remote_action.k.flag = KeyFlag::key_down;
  } else {
    remote_action.k.flag = KeyFlag::key_up;
  }
  remote_action.k.key_value = key_code;

  if (!controlled_remote_id_.empty()) {
    // std::shared_lock lock(client_properties_mutex_);
    if (client_properties_.find(controlled_remote_id_) !=
        client_properties_.end()) {
      auto props = client_properties_[controlled_remote_id_];
      if (props->connection_status_ == ConnectionStatus::Connected) {
        std::string msg = remote_action.to_json();
        if (props->peer_) {
          SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                        props->data_label_.c_str());
        }
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
        remote_action.m.flag = MouseFlag::move;
      }

      std::string msg = remote_action.to_json();
      if (props->peer_) {
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

      std::string msg = remote_action.to_json();
      if (props->peer_) {
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
    if (!props->dst_buffer_) {
      props->dst_buffer_capacity_ = video_frame->size;
      props->dst_buffer_ = new unsigned char[video_frame->size];
    }

    if (props->dst_buffer_capacity_ < video_frame->size) {
      delete props->dst_buffer_;
      props->dst_buffer_capacity_ = video_frame->size;
      props->dst_buffer_ = new unsigned char[video_frame->size];
    }

    memcpy(props->dst_buffer_, video_frame->data, video_frame->size);
    bool need_to_update_render_rect = false;
    if (props->video_width_ != props->video_width_last_ ||
        props->video_height_ != props->video_height_last_) {
      need_to_update_render_rect = true;
      props->video_width_last_ = props->video_width_;
      props->video_height_last_ = props->video_height_;
    }
    props->video_width_ = video_frame->width;
    props->video_height_ = video_frame->height;
    props->video_size_ = video_frame->size;

    if (need_to_update_render_rect) {
      render->UpdateRenderRect();
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
                                   void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
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
  if (render->client_properties_.find(remote_id) !=
      render->client_properties_.end()) {
    // local
    auto props = render->client_properties_.find(remote_id)->second;
    if (remote_action.type == ControlType::host_infomation &&
        props->remote_host_name_.empty()) {
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
        if (!render->need_to_create_stream_window_ &&
            !render->client_properties_.empty()) {
          render->need_to_create_stream_window_ = true;
        }
        props->connection_established_ = true;
        props->stream_render_rect_ = {
            0, (int)render->title_bar_height_,
            (int)render->stream_window_width_,
            (int)(render->stream_window_height_ - render->title_bar_height_)};
        break;
      }
      case ConnectionStatus::Disconnected:
      case ConnectionStatus::Failed:
      case ConnectionStatus::Closed: {
        props->connection_established_ = false;
        props->mouse_control_button_pressed_ = false;
        if (props->dst_buffer_ && props->stream_texture_) {
          memset(props->dst_buffer_, 0, props->dst_buffer_capacity_);
          SDL_UpdateTexture(props->stream_texture_, NULL, props->dst_buffer_,
                            props->texture_width_);
        }
        render->CleanSubStreamWindowProperties(props);

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
        render->need_to_send_host_info_ = true;
        render->start_screen_capturer_ = true;
        render->start_speaker_capturer_ = true;
        // #ifdef CROSSDESK_DEBUG
        //         render->start_mouse_controller_ = false;
        //         render->start_keyboard_capturer_ = false;
        // #else
        render->start_mouse_controller_ = true;
        // #endif
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
          render->start_screen_capturer_ = false;
          render->start_speaker_capturer_ = false;
          render->start_mouse_controller_ = false;
          render->start_keyboard_capturer_ = false;
          render->need_to_send_host_info_ = false;
          if (props) props->connection_established_ = false;
          if (render->audio_capture_) {
            render->StopSpeakerCapturer();
            render->audio_capture_ = false;
          }

          render->connection_status_.erase(remote_id);
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

void Render::NetStatusReport(const char* client_id, size_t client_id_size,
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
    render->client_id_with_password_[sizeof(render->client_id_with_password_) -
                                     1] = '\0';

    LOG_INFO("Use client id [{}] and save id into cache file", id);
    render->SaveSettingsIntoCacheFile();
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