#include "application/gui_application.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rd_log.h"

namespace crossdesk {

void GuiApplication::ProcessSdlEvent(const SDL_Event &event) {
  if (main_ctx_) {
    ImGui::SetCurrentContext(main_ctx_);
    ImGui_ImplSDL3_ProcessEvent(&event);
  } else {
    LOG_ERROR("Main context is null");
    return;
  }

  if (stream_window_inited_) {
    if (stream_ctx_) {
      ImGui::SetCurrentContext(stream_ctx_);
      ImGui_ImplSDL3_ProcessEvent(&event);
    } else {
      LOG_ERROR("Stream context is null");
      return;
    }
  }

  if (server_window_inited_) {
    if (server_ctx_) {
      ImGui::SetCurrentContext(server_ctx_);
      ImGui_ImplSDL3_ProcessEvent(&event);
    } else {
      LOG_ERROR("Server context is null");
      return;
    }
  }

  if (APP_EXIT_EVENT != 0 && event.type == APP_EXIT_EVENT) {
    LOG_INFO("Quit program from system tray");
    if (stream_window_) {
      SDL_SetWindowMouseGrab(stream_window_, false);
    }
#if defined(__linux__) && !defined(__APPLE__)
    if (tray_) {
      tray_->RemoveTrayIcon();
    }
#endif
    exit_ = true;
    return;
  }

  if (clipboard_.event_type() != 0 && event.type == clipboard_.event_type()) {
    clipboard_.ApplyPendingRemoteText();
    return;
  }

  switch (event.type) {
  case SDL_EVENT_QUIT:
    if (stream_window_inited_) {
      LOG_INFO("Destroy stream window");
      SDL_SetWindowMouseGrab(stream_window_, false);
      DestroyStreamWindow();
      DestroyStreamWindowContext();

      {
        // std::shared_lock lock(remote_sessions_mutex_);
        for (auto &[host_name, props] : remote_sessions_) {
          std::shared_ptr<std::vector<unsigned char>> frame_snapshot;
          int video_width = 0;
          int video_height = 0;
          {
            std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
            frame_snapshot = props->front_frame_;
            video_width = props->video_width_;
            video_height = props->video_height_;
          }
          if (frame_snapshot && !frame_snapshot->empty() && video_width > 0 &&
              video_height > 0) {
            thumbnail_->SaveToThumbnail(
                (char *)frame_snapshot->data(), video_width, video_height,
                host_name, props->remote_host_name_,
                props->remember_password_ ? props->remote_password_ : "");
          }

          if (props->peer_) {
            std::string client_id = (host_name == client_id_)
                                        ? "C-" + std::string(client_id_)
                                        : client_id_;
            LOG_INFO("[{}] Leave connection [{}]", client_id, host_name);
            LeaveConnection(props->peer_, host_name.c_str());
            LOG_INFO("Destroy peer [{}]", client_id);
            DestroyPeer(&props->peer_);
          }

          props->streaming_ = false;
          props->remember_password_ = false;
          props->connection_established_ = false;
          props->audio_capture_button_pressed_ = false;

          memset(&props->net_traffic_stats_, 0,
                 sizeof(props->net_traffic_stats_));
          SDL_SetWindowFullscreen(main_window_, false);
          SDL_FlushEvents(STREAM_REFRESH_EVENT, STREAM_REFRESH_EVENT);
          memset(audio_buffer_, 0, 720);
        }
      }

      {
        // std::unique_lock lock(remote_sessions_mutex_);
        remote_sessions_.clear();
      }

      rejoin_ = false;
      is_client_mode_ = false;
      reload_recent_connections_ = true;
      fullscreen_button_pressed_ = false;
      start_keyboard_capturer_ = false;
      just_created_ = false;
      recent_connection_image_save_time_ = SDL_GetTicks();
    } else {
      LOG_INFO("Quit program");
      exit_ = true;
    }
    break;

  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    if (stream_window_ &&
        event.window.windowID == SDL_GetWindowID(stream_window_)) {
      break;
    }

    if (main_window_ &&
        event.window.windowID == SDL_GetWindowID(main_window_) &&
        MinimizeMainWindowToTray()) {
      break;
    }

    exit_ = true;
    break;

  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    if (stream_window_created_ &&
        event.window.windowID == SDL_GetWindowID(stream_window_)) {
      UpdateRenderRect();
    }
    break;

  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    if (stream_window_ &&
        SDL_GetWindowID(stream_window_) == event.window.windowID) {
      focus_on_stream_window_ = true;
    } else if (main_window_ &&
               SDL_GetWindowID(main_window_) == event.window.windowID) {
      foucs_on_main_window_ = true;
    }
    break;

  case SDL_EVENT_WINDOW_FOCUS_LOST:
    if (stream_window_ &&
        SDL_GetWindowID(stream_window_) == event.window.windowID) {
      keyboard_.ForceReleasePressedKeys();
      focus_on_stream_window_ = false;
    } else if (main_window_ &&
               SDL_GetWindowID(main_window_) == event.window.windowID) {
      foucs_on_main_window_ = false;
    }
    break;
  case SDL_EVENT_DROP_FILE:
    transfers_.HandleDropEvent(event);
    break;

  case SDL_EVENT_CLIPBOARD_UPDATE:
    clipboard_.HandleLocalUpdate();
    break;

  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_WHEEL: {
    Uint32 mouse_window_id = 0;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      mouse_window_id = event.motion.windowID;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
               event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      mouse_window_id = event.button.windowID;
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
      mouse_window_id = event.wheel.windowID;
    }

    if (focus_on_stream_window_ && stream_window_ &&
        SDL_GetWindowID(stream_window_) == mouse_window_id) {
      ProcessMouseEvent(event);
    }
    break;
  }

  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    if (keyboard_capturer_is_started_ &&
        keyboard_capturer_uses_window_events_ &&
        focus_on_stream_window_ && stream_window_ &&
        SDL_GetWindowID(stream_window_) == event.key.windowID) {
      ProcessKeyboardEvent(event);
    }
    break;

  default:
    if (event.type == STREAM_REFRESH_EVENT) {
      auto *props = static_cast<RemoteSession *>(event.user.data1);
      if (!props) {
        break;
      }
      std::shared_ptr<std::vector<unsigned char>> frame_snapshot;
      int video_width = 0;
      int video_height = 0;
      bool render_rect_dirty = false;
      bool cleanup_pending = false;
      {
        std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
        cleanup_pending = props->stream_cleanup_pending_;
        if (!cleanup_pending) {
          frame_snapshot = props->front_frame_;
          video_width = props->video_width_;
          video_height = props->video_height_;
        }
        render_rect_dirty = props->render_rect_dirty_;
      }

      if (cleanup_pending) {
        if (props->stream_texture_) {
          SDL_DestroyTexture(props->stream_texture_);
          props->stream_texture_ = nullptr;
        }
        {
          std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
          props->stream_cleanup_pending_ = false;
        }

        if (render_rect_dirty) {
          UpdateRenderRect();
          std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
          props->render_rect_dirty_ = false;
        }
        break;
      }

      if (video_width <= 0 || video_height <= 0) {
        break;
      }
      if (!frame_snapshot || frame_snapshot->empty()) {
        break;
      }

      if (props->stream_texture_) {
        if (video_width != props->texture_width_ ||
            video_height != props->texture_height_) {
          props->texture_width_ = video_width;
          props->texture_height_ = video_height;

          SDL_DestroyTexture(props->stream_texture_);
          // props->stream_texture_ = SDL_CreateTexture(
          //     stream_renderer_, stream_pixformat_,
          //     SDL_TEXTUREACCESS_STREAMING, props->texture_width_,
          //     props->texture_height_);

          SDL_PropertiesID nvProps = SDL_CreateProperties();
          SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,
                                props->texture_width_);
          SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,
                                props->texture_height_);
          SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                                SDL_PIXELFORMAT_NV12);
          SDL_SetNumberProperty(nvProps,
                                SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,
                                SDL_COLORSPACE_BT601_LIMITED);
          props->stream_texture_ =
              SDL_CreateTextureWithProperties(stream_renderer_, nvProps);
          SDL_DestroyProperties(nvProps);
        }
      } else {
        props->texture_width_ = video_width;
        props->texture_height_ = video_height;
        // props->stream_texture_ = SDL_CreateTexture(
        //     stream_renderer_, stream_pixformat_,
        //     SDL_TEXTUREACCESS_STREAMING, props->texture_width_,
        //     props->texture_height_);

        SDL_PropertiesID nvProps = SDL_CreateProperties();
        SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,
                              props->texture_width_);
        SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,
                              props->texture_height_);
        SDL_SetNumberProperty(nvProps, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                              SDL_PIXELFORMAT_NV12);
        SDL_SetNumberProperty(nvProps,
                              SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,
                              SDL_COLORSPACE_BT601_LIMITED);
        props->stream_texture_ =
            SDL_CreateTextureWithProperties(stream_renderer_, nvProps);
        SDL_DestroyProperties(nvProps);
      }

      SDL_UpdateTexture(props->stream_texture_, NULL, frame_snapshot->data(),
                        props->texture_width_);

      if (render_rect_dirty) {
        UpdateRenderRect();
        std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
        props->render_rect_dirty_ = false;
      }
    }
    break;
  }
}


} // namespace crossdesk
