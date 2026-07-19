#include "application/gui_application.h"

#include <cmath>

#include "rd_log.h"

namespace crossdesk {

int GuiApplication::DrawMainWindow() {
  if (!main_ctx_) {
    LOG_ERROR("Main context is null");
    return -1;
  }

  ImGui::SetCurrentContext(main_ctx_);
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGuiIO &io = ImGui::GetIO();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);

  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y),
                           ImGuiCond_Always);
  ImGui::Begin("MainRender", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();

  TitleBar(true);

  MainWindow();

  UpdateNotificationWindow();

#if _WIN32 && CROSSDESK_PORTABLE
  PortableServiceInstallWindow();
#endif

#ifdef __APPLE__
  if (show_request_permission_window_) {
    RequestPermissionWindow();
  }
#endif

  ImGui::End();

  // Rendering
  (void)io;
  ImGui::Render();
  SDL_SetRenderScale(main_renderer_, io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y);
  SDL_SetRenderDrawColor(main_renderer_, 0, 0, 0, 0);
  SDL_RenderClear(main_renderer_);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), main_renderer_);
  SDL_RenderPresent(main_renderer_);

  return 0;
}

int GuiApplication::DrawStreamWindow() {
  if (!stream_ctx_) {
    LOG_ERROR("Stream context is null");
    return -1;
  }

  ImGui::SetCurrentContext(stream_ctx_);
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  StreamWindow();

  ImGuiIO &io = ImGui::GetIO();
  float stream_title_window_height =
      fullscreen_button_pressed_ ? 0 : title_bar_height_;

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  // Set minimum window size to 0 to allow exact height control
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, stream_title_window_height),
                           ImGuiCond_Always);
  ImGui::Begin("StreamTitleWindow", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  if (!fullscreen_button_pressed_) {
    TitleBar(false);
  }

  ImGui::End();

  // Rendering
  (void)io;
  ImGui::Render();
  SDL_SetRenderScale(stream_renderer_, io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y);
  SDL_SetRenderDrawColor(stream_renderer_, 0, 0, 0, 255);
  SDL_RenderClear(stream_renderer_);

  // std::shared_lock lock(remote_sessions_mutex_);
  for (auto &it : remote_sessions_) {
    auto props = it.second;
    if (props->tab_selected_) {
      SDL_FRect render_rect_f = {
          props->stream_render_rect_f_.x, props->stream_render_rect_f_.y,
          props->stream_render_rect_f_.w, props->stream_render_rect_f_.h};
      SDL_RenderTexture(stream_renderer_, props->stream_texture_, NULL,
                        &render_rect_f);
    }
  }
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), stream_renderer_);
  SDL_RenderPresent(stream_renderer_);

  return 0;
}

int GuiApplication::DrawServerWindow() {
  if (!server_ctx_) {
    LOG_ERROR("Server context is null");
    return -1;
  }
  ImGui::SetCurrentContext(server_ctx_);
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGuiIO &io = ImGui::GetIO();
  server_window_width_ = io.DisplaySize.x;
  server_window_height_ = io.DisplaySize.y;

  ServerWindow();
  ImGui::Render();
  SDL_SetRenderScale(server_renderer_, io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y);
  SDL_SetRenderDrawColor(server_renderer_, 0, 0, 0, 0);
  SDL_RenderClear(server_renderer_);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), server_renderer_);
  SDL_RenderPresent(server_renderer_);
  return 0;
}

bool GuiApplication::MinimizeMainWindowToTray() {
  if (!enable_minimize_to_tray_ || !main_window_) {
    return false;
  }

#if defined(_WIN32) || defined(__APPLE__)
  if (!tray_) {
    return false;
  }

  tray_->MinimizeToTray();
  return true;
#elif defined(__linux__) && !defined(__APPLE__)
  if (!tray_) {
    return false;
  }

  return tray_->MinimizeToTray();
#else
  return false;
#endif
}

void GuiApplication::UpdateRenderRect() {
  // std::shared_lock lock(remote_sessions_mutex_);
  for (auto &[_, props] : remote_sessions_) {
    if (!props->reset_control_bar_pos_) {
      props->mouse_diff_control_bar_pos_x_ = 0;
      props->mouse_diff_control_bar_pos_y_ = 0;
    }

    if (!just_created_) {
      props->reset_control_bar_pos_ = true;
    }

    int stream_window_width, stream_window_height;
    SDL_GetWindowSize(stream_window_, &stream_window_width,
                      &stream_window_height);
    stream_window_width_ = (float)stream_window_width;
    stream_window_height_ = (float)stream_window_height;

    float video_ratio =
        (float)props->video_width_ / (float)props->video_height_;
    float video_ratio_reverse =
        (float)props->video_height_ / (float)props->video_width_;

    float render_area_width = props->render_window_width_;
    float render_area_height = props->render_window_height_;

    props->stream_render_rect_last_ = props->stream_render_rect_;

    SDL_FRect rect_f{props->render_window_x_, props->render_window_y_,
                     render_area_width, render_area_height};
    if (render_area_width < render_area_height * video_ratio) {
      rect_f.x = props->render_window_x_;
      rect_f.y = std::abs(render_area_height -
                          render_area_width * video_ratio_reverse) /
                     2.0f +
                 props->render_window_y_;
      rect_f.w = render_area_width;
      rect_f.h = render_area_width * video_ratio_reverse;
    } else if (render_area_width > render_area_height * video_ratio) {
      rect_f.x =
          std::abs(render_area_width - render_area_height * video_ratio) /
              2.0f +
          props->render_window_x_;
      rect_f.y = props->render_window_y_;
      rect_f.w = render_area_height * video_ratio;
      rect_f.h = render_area_height;
    } else {
      rect_f.x = props->render_window_x_;
      rect_f.y = props->render_window_y_;
      rect_f.w = render_area_width;
      rect_f.h = render_area_height;
    }

    props->stream_render_rect_f_ = rect_f;
    props->stream_render_rect_ = {static_cast<int>(std::lround(rect_f.x)),
                                  static_cast<int>(std::lround(rect_f.y)),
                                  static_cast<int>(std::lround(rect_f.w)),
                                  static_cast<int>(std::lround(rect_f.h))};
  }
}

} // namespace crossdesk
