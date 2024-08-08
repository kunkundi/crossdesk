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

int Render::ProcessMouseKeyEven(SDL_Event &ev) {
  if (!control_mouse_) {
    return 0;
  }

  float ratio = (float)(1280.0 / main_window_width_);

  RemoteAction remote_action;
  remote_action.m.x = (size_t)(ev.button.x * ratio);
  remote_action.m.y = (size_t)(ev.button.y * ratio);

  if (SDL_KEYDOWN == ev.type)  // SDL_KEYUP
  {
    // printf("SDLK_DOWN: %d\n", SDL_KeyCode(ev.key.keysym.sym));
    if (SDLK_DOWN == ev.key.keysym.sym) {
      // printf("SDLK_DOWN  \n");

    } else if (SDLK_UP == ev.key.keysym.sym) {
      // printf("SDLK_UP  \n");

    } else if (SDLK_LEFT == ev.key.keysym.sym) {
      // printf("SDLK_LEFT  \n");

    } else if (SDLK_RIGHT == ev.key.keysym.sym) {
      // printf("SDLK_RIGHT  \n");
    }
  } else if (SDL_MOUSEBUTTONDOWN == ev.type) {
    remote_action.type = ControlType::mouse;
    if (SDL_BUTTON_LEFT == ev.button.button) {
      remote_action.m.flag = MouseFlag::left_down;
    } else if (SDL_BUTTON_RIGHT == ev.button.button) {
      remote_action.m.flag = MouseFlag::right_down;
    }
    if (subwindow_hovered_) {
      remote_action.m.flag = MouseFlag::move;
    }
    SendData(peer_, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_MOUSEBUTTONUP == ev.type) {
    remote_action.type = ControlType::mouse;
    if (SDL_BUTTON_LEFT == ev.button.button) {
      remote_action.m.flag = MouseFlag::left_up;
    } else if (SDL_BUTTON_RIGHT == ev.button.button) {
      remote_action.m.flag = MouseFlag::right_up;
    }
    if (subwindow_hovered_) {
      remote_action.m.flag = MouseFlag::move;
    }
    SendData(peer_, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_MOUSEMOTION == ev.type) {
    remote_action.type = ControlType::mouse;
    remote_action.m.flag = MouseFlag::move;
    SendData(peer_, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_QUIT == ev.type) {
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
    printf("SDL_QUIT\n");
    return 0;
  }

  return 0;
}

void Render::SdlCaptureAudioIn(void *userdata, Uint8 *stream, int len) {
  Render *render = (Render *)userdata;
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

void Render::OnReceiveVideoBufferCb(const char *data, size_t size,
                                    const char *user_id, size_t user_id_size,
                                    void *user_data) {
  Render *render = (Render *)user_data;
  if (render->connection_established_) {
    memcpy(render->dst_buffer_, data, size);
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
  render->audio_buffer_fresh_ = true;
  SDL_QueueAudio(render->output_dev_, data, (uint32_t)size);
}

void Render::OnReceiveDataBufferCb(const char *data, size_t size,
                                   const char *user_id, size_t user_id_size,
                                   void *user_data) {
  Render *render = (Render *)user_data;
  std::string user(user_id, user_id_size);
  RemoteAction remote_action;
  memcpy(&remote_action, data, sizeof(remote_action));

#if MOUSE_CONTROL
  if (render->mouse_controller_) {
    render->mouse_controller_->SendCommand(remote_action);
  }
#endif
}

void Render::OnSignalStatusCb(SignalStatus status, void *user_data) {
  Render *render = (Render *)user_data;
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
  }
}

void Render::OnConnectionStatusCb(ConnectionStatus status, void *user_data) {
  Render *render = (Render *)user_data;
  render->connection_status_ = status;
  render->show_connection_status_window_ = true;
  if (ConnectionStatus::Connecting == status) {
    render->connection_status_str_ = "Connecting";
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
    render->exit_video_window_ = false;
    render->remote_password_.clear();
    if (render->dst_buffer_) {
      memset(render->dst_buffer_, 0, 1280 * 720 * 3);
      SDL_UpdateTexture(render->stream_texture_, NULL, render->dst_buffer_,
                        1280);
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

void Render::NetStatusReport(int client_id, TraversalMode mode,
                             const unsigned short send,
                             const unsigned short receive, void *user_data) {
  Render *render = (Render *)user_data;
  if (client_id != 0 && 0 == strcmp(render->client_id_, "")) {
    std::string client_id_s = std::to_string(client_id);
    strncpy(render->client_id_, client_id_s.c_str(),
            sizeof(render->client_id_));
    LOG_INFO("Use client id [{}] and save id into cache file", client_id_s);
    render->SaveSettingsIntoCacheFile();
  }
  if (mode != TraversalMode::UnknownMode) {
    LOG_INFO("Net mode: [{}]", int(mode));
  }
}