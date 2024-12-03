#include "rd_log.h"
#include "render.h"

int Render::ControlWindow(
    std::shared_ptr<SubStreamWindowProperties> &properties) {
  double time_duration =
      ImGui::GetTime() - properties->control_bar_button_pressed_time_;
  if (properties->control_window_width_is_changing_) {
    if (properties->control_bar_expand_) {
      properties->control_window_width_ =
          (float)(properties->control_window_min_width_ +
                  (properties->control_window_max_width_ -
                   properties->control_window_min_width_) *
                      4 * time_duration);
    } else {
      properties->control_window_width_ =
          (float)(properties->control_window_max_width_ -
                  (properties->control_window_max_width_ -
                   properties->control_window_min_width_) *
                      4 * time_duration);
    }
  }

  time_duration =
      ImGui::GetTime() - properties->net_traffic_stats_button_pressed_time_;
  if (properties->control_window_height_is_changing_) {
    if (properties->control_bar_expand_ &&
        properties->net_traffic_stats_button_pressed_) {
      properties->control_window_height_ =
          (float)(properties->control_window_min_height_ +
                  (properties->control_window_max_height_ -
                   properties->control_window_min_height_) *
                      4 * time_duration);
    } else if (properties->control_bar_expand_ &&
               !properties->net_traffic_stats_button_pressed_) {
      properties->control_window_height_ =
          (float)(properties->control_window_max_height_ -
                  (properties->control_window_max_height_ -
                   properties->control_window_min_height_) *
                      4 * time_duration);
    }
  }

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  ImGui::SetNextWindowSize(ImVec2(properties->control_window_width_,
                                  properties->control_window_height_),
                           ImGuiCond_Always);

  if (0 == properties->control_winodw_pos_.x &&
      0 == properties->control_winodw_pos_.y) {
    ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_ + 1), ImGuiCond_Once);
  }

  if (properties->reset_control_bar_pos_) {
    float new_control_window_pos_x, new_control_window_pos_y, new_cursor_pos_x,
        new_cursor_pos_y;

    // set control window pos
    new_control_window_pos_x = properties->control_winodw_pos_.x;
    if (properties->control_winodw_pos_.y <
        properties->stream_render_rect_last_.y) {
      new_control_window_pos_y = properties->stream_render_rect_.y -
                                 (properties->stream_render_rect_last_.y -
                                  properties->control_winodw_pos_.y);
      if (fullscreen_button_pressed_ && new_control_window_pos_y < 0) {
        new_control_window_pos_y = 0;
      } else if (!fullscreen_button_pressed_ &&
                 new_control_window_pos_y < (title_bar_height_ + 1)) {
        new_control_window_pos_y = title_bar_height_ + 1;
      }
    } else if (properties->control_winodw_pos_.y +
                   properties->control_window_height_ >
               properties->stream_render_rect_last_.y +
                   properties->stream_render_rect_last_.h) {
      new_control_window_pos_y = properties->stream_render_rect_.y +
                                 properties->stream_render_rect_.h +
                                 (properties->control_winodw_pos_.y -
                                  properties->stream_render_rect_last_.y -
                                  properties->stream_render_rect_last_.h);
      if (new_control_window_pos_y > properties->sub_stream_window_height_ -
                                         properties->control_window_height_) {
        new_control_window_pos_y = properties->sub_stream_window_height_ -
                                   properties->control_window_height_;
      }
    } else if (properties->control_winodw_pos_.y +
                   properties->control_window_height_ ==
               properties->stream_render_rect_last_.y +
                   properties->stream_render_rect_last_.h) {
      new_control_window_pos_y = properties->stream_render_rect_.y +
                                 properties->stream_render_rect_.h -
                                 properties->control_window_height_;
    } else {
      new_control_window_pos_y =
          (properties->control_winodw_pos_.y -
           properties->stream_render_rect_last_.y) /
              (float)(properties->stream_render_rect_last_.h) *
              properties->stream_render_rect_.h +
          properties->stream_render_rect_.y;
    }

    ImGui::SetNextWindowPos(
        ImVec2(new_control_window_pos_x, new_control_window_pos_y),
        ImGuiCond_Always);

    if (0 != properties->mouse_diff_control_bar_pos_x_ &&
        0 != properties->mouse_diff_control_bar_pos_y_) {
      // set cursor pos
      new_cursor_pos_x =
          new_control_window_pos_x + properties->mouse_diff_control_bar_pos_x_;
      new_cursor_pos_y =
          new_control_window_pos_y + properties->mouse_diff_control_bar_pos_y_;

      SDL_WarpMouseInWindow(stream_window_, (int)new_cursor_pos_x,
                            (int)new_cursor_pos_y);
    }
    properties->reset_control_bar_pos_ = false;
  } else if (!properties->reset_control_bar_pos_ &&
                 ImGui::IsMouseReleased(ImGuiPopupFlags_MouseButtonLeft) ||
             properties->control_window_width_is_changing_) {
    if (properties->control_winodw_pos_.x <=
        properties->sub_stream_window_width_ / 2) {
      float pos_x = 0;
      float pos_y =
          (properties->control_winodw_pos_.y >=
               (fullscreen_button_pressed_ ? 0 : (title_bar_height_ + 1)) &&
           properties->control_winodw_pos_.y <=
               properties->sub_stream_window_height_ -
                   properties->control_window_height_)
              ? properties->control_winodw_pos_.y
              : (properties->control_winodw_pos_.y <
                         (fullscreen_button_pressed_ ? 0
                                                     : (title_bar_height_ + 1))
                     ? (fullscreen_button_pressed_ ? 0
                                                   : (title_bar_height_ + 1))
                     : (properties->sub_stream_window_height_ -
                        properties->control_window_height_));

      if (properties->control_bar_expand_) {
        if (properties->control_window_width_ >=
            properties->control_window_max_width_) {
          properties->control_window_width_ =
              properties->control_window_max_width_;
          properties->control_window_width_is_changing_ = false;
        } else {
          properties->control_window_width_is_changing_ = true;
        }
      } else {
        if (properties->control_window_width_ <=
            properties->control_window_min_width_) {
          properties->control_window_width_ =
              properties->control_window_min_width_;
          properties->control_window_width_is_changing_ = false;
        } else {
          properties->control_window_width_is_changing_ = true;
        }
      }
      ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
      properties->is_control_bar_in_left_ = true;
    } else if (properties->control_winodw_pos_.x >
               properties->sub_stream_window_width_ / 2) {
      float pos_x = 0;
      float pos_y =
          (properties->control_winodw_pos_.y >=
               (fullscreen_button_pressed_ ? 0 : (title_bar_height_ + 1)) &&
           properties->control_winodw_pos_.y <=
               properties->sub_stream_window_height_ -
                   properties->control_window_height_)
              ? properties->control_winodw_pos_.y
              : (properties->control_winodw_pos_.y <
                         (fullscreen_button_pressed_ ? 0
                                                     : (title_bar_height_ + 1))
                     ? (fullscreen_button_pressed_ ? 0
                                                   : (title_bar_height_ + 1))
                     : (properties->sub_stream_window_height_ -
                        properties->control_window_height_));

      if (properties->control_bar_expand_) {
        if (properties->control_window_width_ >=
            properties->control_window_max_width_) {
          properties->control_window_width_ =
              properties->control_window_max_width_;
          properties->control_window_width_is_changing_ = false;
          pos_x = properties->sub_stream_window_width_ -
                  properties->control_window_max_width_;
        } else {
          properties->control_window_width_is_changing_ = true;
          pos_x = properties->sub_stream_window_width_ -
                  properties->control_window_width_;
        }
      } else {
        if (properties->control_window_width_ <=
            properties->control_window_min_width_) {
          properties->control_window_width_ =
              properties->control_window_min_width_;
          properties->control_window_width_is_changing_ = false;
          pos_x = properties->sub_stream_window_width_ -
                  properties->control_window_min_width_;
        } else {
          properties->control_window_width_is_changing_ = true;
          pos_x = properties->sub_stream_window_width_ -
                  properties->control_window_width_;
        }
      }
      ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
      properties->is_control_bar_in_left_ = false;
    }
  }

  if (properties->control_bar_expand_ &&
      properties->control_window_height_is_changing_) {
    if (properties->net_traffic_stats_button_pressed_) {
      if (properties->control_window_height_ >=
          properties->control_window_max_height_) {
        properties->control_window_height_ =
            properties->control_window_max_height_;
        properties->control_window_height_is_changing_ = false;
      } else {
        properties->control_window_height_is_changing_ = true;
      }
    } else {
      if (properties->control_window_height_ <=
          properties->control_window_min_height_) {
        properties->control_window_height_ =
            properties->control_window_min_height_;
        properties->control_window_height_is_changing_ = false;
      } else {
        properties->control_window_height_is_changing_ = true;
      }
    }
  }

  ImGui::Begin("ControlWindow", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();

  properties->control_winodw_pos_ = ImGui::GetWindowPos();
  SDL_GetMouseState(&properties->mouse_pos_x_, &properties->mouse_pos_y_);
  properties->mouse_diff_control_bar_pos_x_ =
      properties->mouse_pos_x_ - properties->control_winodw_pos_.x;
  properties->mouse_diff_control_bar_pos_y_ =
      properties->mouse_pos_y_ - properties->control_winodw_pos_.y;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  static bool a, b, c, d, e;
  ImGui::SetNextWindowPos(ImVec2(properties->is_control_bar_in_left_
                                     ? properties->control_winodw_pos_.x -
                                           properties->control_window_width_
                                     : properties->control_winodw_pos_.x,
                                 properties->control_winodw_pos_.y),
                          ImGuiCond_Always);
  ImGui::SetWindowFontScale(0.5f);

  ImGui::BeginChild("ControlBar",
                    ImVec2(properties->control_window_width_ * 2,
                           properties->control_window_height_),
                    ImGuiChildFlags_Border, ImGuiWindowFlags_NoDecoration);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();

  ControlBar(properties);
  properties->control_bar_hovered_ = ImGui::IsWindowHovered();

  ImGui::EndChild();
  ImGui::End();
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor();

  return 0;
}