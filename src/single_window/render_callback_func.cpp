#include "device_controller.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

#ifdef REMOTE_DESK_DEBUG
#else
#define MOUSE_CONTROL 1
#endif

int Render::ProcessMouseKeyEvent(SDL_Event &event) {
  if (!control_mouse_ || !connection_established_) {
    return 0;
  }

  if (SDL_KEYDOWN == event.type || SDL_KEYUP == event.type) {
    ProcessKeyEvent(event);
  } else {
    ProcessMouseEvent(event);
  }

  return 0;
}

int Render::ProcessMouseEvent(SDL_Event &event) {
  float ratio_x = (float)video_width_ / (float)stream_render_rect_.w;
  float ratio_y = (float)video_height_ / (float)stream_render_rect_.h;

  if (event.button.x <= stream_render_rect_.x) {
    event.button.x = 0;
  } else if (event.button.x > stream_render_rect_.x &&
             event.button.x < stream_render_rect_.x + stream_render_rect_.w) {
    event.button.x -= stream_render_rect_.x;
  } else if (event.button.x >= stream_render_rect_.x + stream_render_rect_.w) {
    event.button.x = stream_render_rect_.w;
  }

  if (event.button.y <= stream_render_rect_.y) {
    event.button.y = 0;
  } else if (event.button.y > stream_render_rect_.y &&
             event.button.y < stream_render_rect_.y + stream_render_rect_.h) {
    event.button.y -= stream_render_rect_.y;
  } else if (event.button.y >= stream_render_rect_.y + stream_render_rect_.h) {
    event.button.y = stream_render_rect_.h;
  }

  RemoteAction remote_action;
  remote_action.m.x = (size_t)(event.button.x * ratio_x);
  remote_action.m.y = (size_t)(event.button.y * ratio_y);

  if (SDL_MOUSEBUTTONDOWN == event.type) {
    remote_action.type = ControlType::mouse;
    if (SDL_BUTTON_LEFT == event.button.button) {
      remote_action.m.flag = MouseFlag::left_down;
    } else if (SDL_BUTTON_RIGHT == event.button.button) {
      remote_action.m.flag = MouseFlag::right_down;
    }
    if (control_bar_hovered_) {
      remote_action.m.flag = MouseFlag::move;
    }
    SendData(peer_, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_MOUSEBUTTONUP == event.type) {
    remote_action.type = ControlType::mouse;
    if (SDL_BUTTON_LEFT == event.button.button) {
      remote_action.m.flag = MouseFlag::left_up;
    } else if (SDL_BUTTON_RIGHT == event.button.button) {
      remote_action.m.flag = MouseFlag::right_up;
    }
    if (control_bar_hovered_) {
      remote_action.m.flag = MouseFlag::move;
    }
    SendData(peer_, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_MOUSEMOTION == event.type) {
    remote_action.type = ControlType::mouse;
    remote_action.m.flag = MouseFlag::move;
    SendData(peer_, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  }

  return 0;
}

int Render::ProcessKeyEvent(SDL_Event &event) {
  RemoteAction remote_action;
  SDL_Keycode key = event.key.keysym.sym;
  if (SDL_KEYDOWN == event.type) {
    std::cout << "Key pressed: " << SDL_GetKeyName(key) << std::endl;
  } else if (SDL_KEYUP == event.type) {
    std::cout << "Key released: " << SDL_GetKeyName(key) << std::endl;
  }

  return 0;
}

void Render::SdlCaptureAudioIn(void *userdata, Uint8 *stream, int len) {
  Render *render = (Render *)userdata;
  if (!render) {
    return;
  }

  if (1) {
    if ("Connected" == render->connection_status_str_) {
      SendData(render->peer_, DATA_TYPE::AUDIO, (const char *)stream, len);
    }
  } else {
    memcpy(render->audio_buffer_, stream, len);
    render->audio_len_ = len;
    SDL_Delay(10);
    render->audio_buffer_fresh_ = true;
  }
}

void Render::SdlCaptureAudioOut(void *userdata, Uint8 *stream, int len) {
  // Render *render = (Render *)userdata;
  // if ("Connected" == render->connection_status_str_) {
  //   SendData(render->peer_, DATA_TYPE::AUDIO, (const char *)stream, len);
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

void Render::OnReceiveVideoBufferCb(const XVideoFrame *video_frame,
                                    const char *user_id, size_t user_id_size,
                                    void *user_data) {
  Render *render = (Render *)user_data;
  if (!render) {
    return;
  }

  if (render->connection_established_) {
    if (!render->dst_buffer_) {
      render->dst_buffer_capacity_ = video_frame->size;
      render->dst_buffer_ = new unsigned char[video_frame->size];
      // Adapt stream_render_rect_ to the video resolution
      SDL_Event event;
      event.type = SDL_WINDOWEVENT;
      event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
      SDL_PushEvent(&event);
    }

    if (render->dst_buffer_capacity_ < video_frame->size) {
      delete render->dst_buffer_;
      render->dst_buffer_capacity_ = video_frame->size;
      render->dst_buffer_ = new unsigned char[video_frame->size];
    }

    memcpy(render->dst_buffer_, video_frame->data, video_frame->size);
    render->video_width_ = video_frame->width;
    render->video_height_ = video_frame->height;
    render->video_size_ = video_frame->size;

    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    render->received_frame_ = true;
    render->streaming_ = true;
  }
}

void Render::OnReceiveAudioBufferCb(const char *data, size_t size,
                                    const char *user_id, size_t user_id_size,
                                    void *user_data) {
  Render *render = (Render *)user_data;
  if (!render) {
    return;
  }

  render->audio_buffer_fresh_ = true;
  SDL_QueueAudio(render->output_dev_, data, (uint32_t)size);
}

void Render::OnReceiveDataBufferCb(const char *data, size_t size,
                                   const char *user_id, size_t user_id_size,
                                   void *user_data) {
  Render *render = (Render *)user_data;
  if (!render) {
    return;
  }

  std::string user(user_id, user_id_size);
  RemoteAction remote_action;
  memcpy(&remote_action, data, sizeof(remote_action));

  if (ControlType::mouse == remote_action.type && render->mouse_controller_) {
    render->mouse_controller_->SendCommand(remote_action);
  } else if (ControlType::audio_capture == remote_action.type) {
    if (remote_action.a) {
      render->StartSpeakerCapture();
    } else {
      render->StopSpeakerCapture();
    }
  }
}

void Render::OnSignalStatusCb(SignalStatus status, void *user_data) {
  Render *render = (Render *)user_data;
  if (!render) {
    return;
  }

  render->signal_status_ = status;
  if (SignalStatus::SignalConnecting == status) {
    render->signal_status_str_ = "SignalConnecting";
    render->signal_connected_ = false;
  } else if (SignalStatus::SignalConnected == status) {
    render->signal_status_str_ = "SignalConnected";
    render->signal_connected_ = true;
  } else if (SignalStatus::SignalFailed == status) {
    render->signal_status_str_ = "SignalFailed";
    render->signal_connected_ = false;
  } else if (SignalStatus::SignalClosed == status) {
    render->signal_status_str_ = "SignalClosed";
    render->signal_connected_ = false;
  } else if (SignalStatus::SignalReconnecting == status) {
    render->signal_status_str_ = "SignalReconnecting";
    render->signal_connected_ = false;
  } else if (SignalStatus::SignalServerClosed == status) {
    render->signal_status_str_ = "SignalServerClosed";
    render->signal_connected_ = false;
    render->is_create_connection_ = false;
  }
}

void Render::OnConnectionStatusCb(ConnectionStatus status, const char *user_id,
                                  const size_t user_id_size, void *user_data) {
  Render *render = (Render *)user_data;
  if (!render) {
    return;
  }

  render->connection_status_ = status;
  render->show_connection_status_window_ = true;
  if (ConnectionStatus::Connecting == status) {
    render->connection_status_str_ = "Connecting";
  } else if (ConnectionStatus::Gathering == status) {
    render->connection_status_str_ = "Gathering";
  } else if (ConnectionStatus::Connected == status) {
    render->connection_status_str_ = "Connected";
    render->connection_established_ = true;
    if (render->peer_reserved_ || !render->is_client_mode_) {
      render->start_screen_capture_ = true;
      render->start_mouse_control_ = true;
    }
  } else if (ConnectionStatus::Disconnected == status) {
    render->connection_status_str_ = "Disconnected";
    render->password_validating_time_ = 0;
  } else if (ConnectionStatus::Failed == status) {
    render->connection_status_str_ = "Failed";
    render->password_validating_time_ = 0;
  } else if (ConnectionStatus::Closed == status) {
    render->connection_status_str_ = "Closed";
    render->password_validating_time_ = 0;
    render->start_screen_capture_ = false;
    render->start_mouse_control_ = false;
    render->connection_established_ = false;
    render->control_mouse_ = false;
    if (render->audio_capture_) {
      render->StopSpeakerCapture();
      render->audio_capture_ = false;
      render->audio_capture_button_pressed_ = false;
    }
    render->exit_video_window_ = false;
    if (!render->rejoin_) {
      memset(render->remote_password_, 0, sizeof(render->remote_password_));
    }
    if (render->dst_buffer_) {
      memset(render->dst_buffer_, 0, render->dst_buffer_capacity_);
      SDL_UpdateTexture(render->stream_texture_, NULL, render->dst_buffer_,
                        render->texture_width_);
    }
  } else if (ConnectionStatus::IncorrectPassword == status) {
    render->connection_status_str_ = "Incorrect password";
    render->password_validating_ = false;
    render->password_validating_time_++;
    if (render->connect_button_pressed_) {
      render->connection_established_ = false;
      render->connect_button_label_ =
          render->connect_button_pressed_
              ? localization::disconnect[render->localization_language_index_]
              : localization::connect[render->localization_language_index_];
    }
  } else if (ConnectionStatus::NoSuchTransmissionId == status) {
    render->connection_status_str_ = "No such transmission id";
    if (render->connect_button_pressed_) {
      render->connection_established_ = false;
      render->connect_button_label_ =
          render->connect_button_pressed_
              ? localization::disconnect[render->localization_language_index_]
              : localization::connect[render->localization_language_index_];
    }
  }
}

void Render::NetStatusReport(const char *client_id, size_t client_id_size,
                             TraversalMode mode, const unsigned short send,
                             const unsigned short receive, void *user_data) {
  Render *render = (Render *)user_data;
  if (!render) {
    return;
  }

  if (0 == strcmp(render->client_id_, "")) {
    memset(&render->client_id_, 0, sizeof(render->client_id_));
    strncpy(render->client_id_, client_id, client_id_size);
    LOG_INFO("Use client id [{}] and save id into cache file", client_id);
    render->SaveSettingsIntoCacheFile();
  }
  if (mode != TraversalMode::UnknownMode) {
    LOG_INFO("Net mode: [{}]", int(mode));
  }
}