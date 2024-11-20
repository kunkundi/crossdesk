#include "rd_log.h"
#include "render.h"

int Render::ControlWindow() {
  auto time_duration = ImGui::GetTime() - control_bar_button_pressed_time_;
  if (control_window_width_is_changing_) {
    if (control_bar_expand_) {
      control_window_width_ =
          control_window_min_width_ +
          (control_window_max_width_ - control_window_min_width_) * 4 *
              time_duration;
    } else {
      control_window_width_ =
          control_window_max_width_ -
          (control_window_max_width_ - control_window_min_width_) * 4 *
              time_duration;
    }
  }

  time_duration = ImGui::GetTime() - net_traffic_stats_button_pressed_time_;
  if (control_window_height_is_changing_) {
    if (control_bar_expand_ && net_traffic_stats_button_pressed_) {
      control_window_height_ =
          control_window_min_height_ +
          (control_window_max_height_ - control_window_min_height_) * 4 *
              time_duration;
    } else if (control_bar_expand_ && !net_traffic_stats_button_pressed_) {
      control_window_height_ =
          control_window_max_height_ -
          (control_window_max_height_ - control_window_min_height_) * 4 *
              time_duration;
    }
  }

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  ImGui::SetNextWindowSize(
      ImVec2(control_window_width_, control_window_height_), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_ + 1), ImGuiCond_Once);

  if (reset_control_bar_pos_) {
    int new_control_window_pos_x, new_control_window_pos_y, new_cursor_pos_x,
        new_cursor_pos_y;

    // set control window pos
    new_control_window_pos_x = control_winodw_pos_.x;
    if (control_winodw_pos_.y < stream_render_rect_last_.y) {
      new_control_window_pos_y =
          stream_render_rect_.y -
          (stream_render_rect_last_.y - control_winodw_pos_.y);
      if (fullscreen_button_pressed_ && new_control_window_pos_y < 0) {
        new_control_window_pos_y = 0;
      } else if (!fullscreen_button_pressed_ &&
                 new_control_window_pos_y < (title_bar_height_ + 1)) {
        new_control_window_pos_y = title_bar_height_ + 1;
      }
    } else if (control_winodw_pos_.y + control_window_height_ >
               stream_render_rect_last_.y + stream_render_rect_last_.h) {
      new_control_window_pos_y =
          stream_render_rect_.y + stream_render_rect_.h +
          (control_winodw_pos_.y - stream_render_rect_last_.y -
           stream_render_rect_last_.h);
      if (new_control_window_pos_y >
          stream_window_height_ - control_window_height_) {
        new_control_window_pos_y =
            stream_window_height_ - control_window_height_;
      }
    } else if (control_winodw_pos_.y + control_window_height_ ==
               stream_render_rect_last_.y + stream_render_rect_last_.h) {
      new_control_window_pos_y = stream_render_rect_.y + stream_render_rect_.h -
                                 control_window_height_;
    } else {
      new_control_window_pos_y =
          (control_winodw_pos_.y - stream_render_rect_last_.y) /
              (float)(stream_render_rect_last_.h) * stream_render_rect_.h +
          stream_render_rect_.y;
    }

    // set cursor pos
    new_cursor_pos_x = new_control_window_pos_x + mouse_diff_control_bar_pos_x_;
    new_cursor_pos_y = new_control_window_pos_y + mouse_diff_control_bar_pos_y_;

    ImGui::SetNextWindowPos(
        ImVec2(new_control_window_pos_x, new_control_window_pos_y),
        ImGuiCond_Always);
    SDL_WarpMouseInWindow(stream_window_, new_cursor_pos_x, new_cursor_pos_y);
    reset_control_bar_pos_ = false;
  } else if (!reset_control_bar_pos_ &&
                 ImGui::IsMouseReleased(ImGuiPopupFlags_MouseButtonLeft) ||
             control_window_width_is_changing_) {
    if (control_winodw_pos_.x <= stream_window_width_ / 2) {
      int pos_x = 0;
      int pos_y =
          (control_winodw_pos_.y >=
               (fullscreen_button_pressed_ ? 0 : (title_bar_height_ + 1)) &&
           control_winodw_pos_.y <=
               stream_window_height_ - control_window_height_)
              ? control_winodw_pos_.y
              : (control_winodw_pos_.y < (fullscreen_button_pressed_
                                              ? 0
                                              : (title_bar_height_ + 1))
                     ? (fullscreen_button_pressed_ ? 0
                                                   : (title_bar_height_ + 1))
                     : (stream_window_height_ - control_window_height_));

      if (control_bar_expand_) {
        if (control_window_width_ >= control_window_max_width_) {
          control_window_width_ = control_window_max_width_;
          control_window_width_is_changing_ = false;
        } else {
          control_window_width_is_changing_ = true;
        }
      } else {
        if (control_window_width_ <= control_window_min_width_) {
          control_window_width_ = control_window_min_width_;
          control_window_width_is_changing_ = false;
        } else {
          control_window_width_is_changing_ = true;
        }
      }
      ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
      is_control_bar_in_left_ = true;
    } else if (control_winodw_pos_.x > stream_window_width_ / 2) {
      int pos_x = 0;
      int pos_y =
          (control_winodw_pos_.y >=
               (fullscreen_button_pressed_ ? 0 : (title_bar_height_ + 1)) &&
           control_winodw_pos_.y <=
               stream_window_height_ - control_window_height_)
              ? control_winodw_pos_.y
              : (control_winodw_pos_.y < (fullscreen_button_pressed_
                                              ? 0
                                              : (title_bar_height_ + 1))
                     ? (fullscreen_button_pressed_ ? 0
                                                   : (title_bar_height_ + 1))
                     : (stream_window_height_ - control_window_height_));

      if (control_bar_expand_) {
        if (control_window_width_ >= control_window_max_width_) {
          control_window_width_ = control_window_max_width_;
          control_window_width_is_changing_ = false;
          pos_x = stream_window_width_ - control_window_max_width_;
        } else {
          control_window_width_is_changing_ = true;
          pos_x = stream_window_width_ - control_window_width_;
        }
      } else {
        if (control_window_width_ <= control_window_min_width_) {
          control_window_width_ = control_window_min_width_;
          control_window_width_is_changing_ = false;
          pos_x = stream_window_width_ - control_window_min_width_;
        } else {
          control_window_width_is_changing_ = true;
          pos_x = stream_window_width_ - control_window_width_;
        }
      }
      ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
      is_control_bar_in_left_ = false;
    }
  }

  if (control_bar_expand_ && control_window_height_is_changing_) {
    if (net_traffic_stats_button_pressed_) {
      if (control_window_height_ >= control_window_max_height_) {
        control_window_height_ = control_window_max_height_;
        control_window_height_is_changing_ = false;
      } else {
        control_window_height_is_changing_ = true;
      }
    } else {
      if (control_window_height_ <= control_window_min_height_) {
        control_window_height_ = control_window_min_height_;
        control_window_height_is_changing_ = false;
      } else {
        control_window_height_is_changing_ = true;
      }
    }
  }

  ImGui::Begin("ControlWindow", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();

  control_winodw_pos_ = ImGui::GetWindowPos();
  SDL_GetMouseState(&mouse_pos_x_, &mouse_pos_y_);
  mouse_diff_control_bar_pos_x_ = mouse_pos_x_ - control_winodw_pos_.x;
  mouse_diff_control_bar_pos_y_ = mouse_pos_y_ - control_winodw_pos_.y;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  static bool a, b, c, d, e;
  ImGui::SetNextWindowPos(
      ImVec2(is_control_bar_in_left_
                 ? control_winodw_pos_.x - control_window_width_
                 : control_winodw_pos_.x,
             control_winodw_pos_.y),
      ImGuiCond_Always);
  ImGui::SetWindowFontScale(0.5f);

  ImGui::BeginChild("ControlBar",
                    ImVec2(control_window_width_ * 2, control_window_height_),
                    ImGuiChildFlags_Border, ImGuiWindowFlags_NoDecoration);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();

  ControlBar();
  control_bar_hovered_ = ImGui::IsWindowHovered();

  ImGui::EndChild();
  ImGui::End();
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor();

  return 0;
}