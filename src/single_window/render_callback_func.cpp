#include "device_controller.h"
#include "localization.h"
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
  if (1) {
    if ("Connected" == connection_status_str_) {
      SendData(peer_, DATA_TYPE::AUDIO, (const char *)stream, len);
    }
  } else {
    memcpy(audio_buffer_, stream, len);
    audio_len_ = len;
    SDL_Delay(10);
    audio_buffer_fresh_ = true;
  }
}

void Render::SdlCaptureAudioOut(void *userdata, Uint8 *stream, int len) {
  if (!audio_buffer_fresh_) {
    return;
  }

  SDL_memset(stream, 0, len);

  if (audio_len_ == 0) {
    return;
  } else {
  }

  len = (len > audio_len_ ? audio_len_ : len);
  SDL_MixAudioFormat(stream, audio_buffer_, AUDIO_S16LSB, len,
                     SDL_MIX_MAXVOLUME);
  audio_buffer_fresh_ = false;
}

void Render::OnReceiveVideoBufferCb(const char *data, size_t size,
                                    const char *user_id, size_t user_id_size,
                                    void *user_data) {
  Render *main_window = (Render *)user_data;
  if (main_window->connection_established_) {
    memcpy(main_window->dst_buffer_, data, size);
    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    main_window->received_frame_ = true;
  }
}

void Render::OnReceiveAudioBufferCb(const char *data, size_t size,
                                    const char *user_id, size_t user_id_size,
                                    void *user_data) {
  Render *main_window = (Render *)user_data;
  main_window->audio_buffer_fresh_ = true;
  SDL_QueueAudio(main_window->output_dev_, data, (uint32_t)size);
}

void Render::OnReceiveDataBufferCb(const char *data, size_t size,
                                   const char *user_id, size_t user_id_size,
                                   void *user_data) {
  Render *main_window = (Render *)user_data;
  std::string user(user_id, user_id_size);
  RemoteAction remote_action;
  memcpy(&remote_action, data, sizeof(remote_action));

#if MOUSE_CONTROL
  if (main_window->mouse_controller_) {
    main_window->mouse_controller_->SendCommand(remote_action);
  }
#endif
}

void Render::OnSignalStatusCb(SignalStatus status, void *user_data) {
  Render *main_window = (Render *)user_data;
  main_window->signal_status_ = status;
  if (SignalStatus::SignalConnecting == status) {
    main_window->signal_status_str_ = "SignalConnecting";
  } else if (SignalStatus::SignalConnected == status) {
    main_window->signal_status_str_ = "SignalConnected";
  } else if (SignalStatus::SignalFailed == status) {
    main_window->signal_status_str_ = "SignalFailed";
  } else if (SignalStatus::SignalClosed == status) {
    main_window->signal_status_str_ = "SignalClosed";
  } else if (SignalStatus::SignalReconnecting == status) {
    main_window->signal_status_str_ = "SignalReconnecting";
  }
}

void Render::OnConnectionStatusCb(ConnectionStatus status, void *user_data) {
  Render *main_window = (Render *)user_data;
  main_window->connection_status_ = status;
  if (ConnectionStatus::Connecting == status) {
    main_window->connection_status_str_ = "Connecting";
  } else if (ConnectionStatus::Connected == status) {
    main_window->connection_status_str_ = "Connected";
    main_window->connection_established_ = true;
    if (!main_window->is_client_mode_) {
      main_window->start_screen_capture_ = true;
      main_window->start_mouse_control_ = true;
    }
  } else if (ConnectionStatus::Disconnected == status) {
    main_window->connection_status_str_ = "Disconnected";
  } else if (ConnectionStatus::Failed == status) {
    main_window->connection_status_str_ = "Failed";
  } else if (ConnectionStatus::Closed == status) {
    main_window->connection_status_str_ = "Closed";
    main_window->start_screen_capture_ = false;
    main_window->start_mouse_control_ = false;
    main_window->connection_established_ = false;
    main_window->control_mouse_ = false;
    if (main_window->dst_buffer_) {
      memset(main_window->dst_buffer_, 0, 1280 * 720 * 3);
      SDL_UpdateTexture(main_window->sdl_texture_, NULL,
                        main_window->dst_buffer_, 1280);
    }
  } else if (ConnectionStatus::IncorrectPassword == status) {
    main_window->connection_status_str_ = "Incorrect password";
    if (main_window->connect_button_pressed_) {
      main_window->connect_button_pressed_ = false;
      main_window->connection_established_ = false;
      main_window->connect_button_label_ =
          main_window->connect_button_pressed_
              ? localization::disconnect[main_window
                                             ->localization_language_index_]
              : localization::connect[main_window
                                          ->localization_language_index_];
    }
  } else if (ConnectionStatus::NoSuchTransmissionId == status) {
    main_window->connection_status_str_ = "No such transmission id";
    if (main_window->connect_button_pressed_) {
      main_window->connect_button_pressed_ = false;
      main_window->connection_established_ = false;
      main_window->connect_button_label_ =
          main_window->connect_button_pressed_
              ? localization::disconnect[main_window
                                             ->localization_language_index_]
              : localization::connect[main_window
                                          ->localization_language_index_];
    }
  }
}
