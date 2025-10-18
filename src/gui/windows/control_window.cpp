#include "rd_log.h"
#include "render.h"

int Render::ControlWindow(std::shared_ptr<SubStreamWindowProperties> &props) {
  double time_duration =
      ImGui::GetTime() - props->control_bar_button_pressed_time_;
  if (props->control_window_width_is_changing_) {
    if (props->control_bar_expand_) {
      props->control_window_width_ =
          (float)(props->control_window_min_width_ +
                  (props->control_window_max_width_ -
                   props->control_window_min_width_) *
                      4 * time_duration);
    } else {
      props->control_window_width_ =
          (float)(props->control_window_max_width_ -
                  (props->control_window_max_width_ -
                   props->control_window_min_width_) *
                      4 * time_duration);
    }
  }

  time_duration =
      ImGui::GetTime() - props->net_traffic_stats_button_pressed_time_;
  if (props->control_window_height_is_changing_) {
    if (props->control_bar_expand_ &&
        props->net_traffic_stats_button_pressed_) {
      props->control_window_height_ =
          (float)(props->control_window_min_height_ +
                  (props->control_window_max_height_ -
                   props->control_window_min_height_) *
                      4 * time_duration);
    } else if (props->control_bar_expand_ &&
               !props->net_traffic_stats_button_pressed_) {
      props->control_window_height_ =
          (float)(props->control_window_max_height_ -
                  (props->control_window_max_height_ -
                   props->control_window_min_height_) *
                      4 * time_duration);
    }
  }

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  ImGui::SetNextWindowSize(
      ImVec2(props->control_window_width_, props->control_window_height_),
      ImGuiCond_Always);

  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_ + 1), ImGuiCond_Once);

  float pos_x = 0;
  float pos_y = 0;
  float y_boundary = fullscreen_button_pressed_ ? 0 : (title_bar_height_ + 1);

  if (props->reset_control_bar_pos_) {
    float new_cursor_pos_x = 0;
    float new_cursor_pos_y = 0;

    // set control window pos
    if (props->control_window_pos_.y + props->control_window_height_ >
        stream_window_height_) {
      pos_y = stream_window_height_ - props->control_window_height_;
    } else if (props->control_window_pos_.y < y_boundary) {
      pos_y = y_boundary;
    } else {
      pos_y = props->control_window_pos_.y;
    }

    if (props->is_control_bar_in_left_) {
      pos_x = 0;
    } else {
      pos_x = stream_window_width_ - props->control_window_width_;
    }

    ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);

    if (0 != props->mouse_diff_control_bar_pos_x_ &&
        0 != props->mouse_diff_control_bar_pos_y_) {
      // set cursor pos
      new_cursor_pos_x = pos_x + props->mouse_diff_control_bar_pos_x_;
      new_cursor_pos_y = pos_y + props->mouse_diff_control_bar_pos_y_;

      SDL_WarpMouseInWindow(stream_window_, (int)new_cursor_pos_x,
                            (int)new_cursor_pos_y);
    }
    props->reset_control_bar_pos_ = false;
  } else if (!props->reset_control_bar_pos_ &&
                 ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
             props->control_window_width_is_changing_) {
    if (props->control_window_pos_.x <= stream_window_width_ / 2) {
      if (props->control_window_pos_.y + props->control_window_height_ >
          stream_window_height_) {
        pos_y = stream_window_height_ - props->control_window_height_;
      } else {
        pos_y = props->control_window_pos_.y;
      }

      if (props->control_bar_expand_) {
        if (props->control_window_width_ >= props->control_window_max_width_) {
          props->control_window_width_ = props->control_window_max_width_;
          props->control_window_width_is_changing_ = false;
        } else {
          props->control_window_width_is_changing_ = true;
        }
      } else {
        if (props->control_window_width_ <= props->control_window_min_width_) {
          props->control_window_width_ = props->control_window_min_width_;
          props->control_window_width_is_changing_ = false;
        } else {
          props->control_window_width_is_changing_ = true;
        }
      }
      props->is_control_bar_in_left_ = true;
    } else if (props->control_window_pos_.x > stream_window_width_ / 2) {
      pos_x = 0;
      pos_y =
          (props->control_window_pos_.y >= y_boundary &&
           props->control_window_pos_.y <=
               stream_window_height_ - props->control_window_height_)
              ? props->control_window_pos_.y
              : (props->control_window_pos_.y < (fullscreen_button_pressed_
                                                     ? 0
                                                     : (title_bar_height_ + 1))
                     ? (fullscreen_button_pressed_ ? 0
                                                   : (title_bar_height_ + 1))
                     : (stream_window_height_ - props->control_window_height_));

      if (props->control_bar_expand_) {
        if (props->control_window_width_ >= props->control_window_max_width_) {
          props->control_window_width_ = props->control_window_max_width_;
          props->control_window_width_is_changing_ = false;
          pos_x = stream_window_width_ - props->control_window_max_width_;
        } else {
          props->control_window_width_is_changing_ = true;
          pos_x = stream_window_width_ - props->control_window_width_;
        }
      } else {
        if (props->control_window_width_ <= props->control_window_min_width_) {
          props->control_window_width_ = props->control_window_min_width_;
          props->control_window_width_is_changing_ = false;
          pos_x = stream_window_width_ - props->control_window_min_width_;
        } else {
          props->control_window_width_is_changing_ = true;
          pos_x = stream_window_width_ - props->control_window_width_;
        }
      }
      props->is_control_bar_in_left_ = false;
    }

    if (props->control_window_pos_.y + props->control_window_height_ >
        stream_window_height_) {
      pos_y = stream_window_height_ - props->control_window_height_;
    } else if (props->control_window_pos_.y < y_boundary) {
      pos_y = y_boundary;
    }
    ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
  }

  if (props->control_bar_expand_ && props->control_window_height_is_changing_) {
    if (props->net_traffic_stats_button_pressed_) {
      if (props->control_window_height_ >= props->control_window_max_height_) {
        props->control_window_height_ = props->control_window_max_height_;
        props->control_window_height_is_changing_ = false;
      } else {
        props->control_window_height_is_changing_ = true;
      }
    } else {
      if (props->control_window_height_ <= props->control_window_min_height_) {
        props->control_window_height_ = props->control_window_min_height_;
        props->control_window_height_is_changing_ = false;
      } else {
        props->control_window_height_is_changing_ = true;
      }
    }
  }

  std::string control_window_title = props->remote_id_ + "ControlWindow";
  ImGui::Begin(control_window_title.c_str(), nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleVar();

  props->control_window_pos_ = ImGui::GetWindowPos();
  SDL_GetMouseState(&props->mouse_pos_x_, &props->mouse_pos_y_);
  props->mouse_diff_control_bar_pos_x_ =
      props->mouse_pos_x_ - props->control_window_pos_.x;
  props->mouse_diff_control_bar_pos_y_ =
      props->mouse_pos_y_ - props->control_window_pos_.y;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  static bool a, b, c, d, e;
  ImGui::SetNextWindowPos(
      ImVec2(props->is_control_bar_in_left_
                 ? props->control_window_pos_.x - props->control_window_width_
                 : props->control_window_pos_.x,
             props->control_window_pos_.y),
      ImGuiCond_Always);
  ImGui::SetWindowFontScale(0.5f);

  std::string control_child_window_title =
      props->remote_id_ + "ControlChildWindow";
  ImGui::BeginChild(
      control_child_window_title.c_str(),
      ImVec2(props->control_window_width_ * 2, props->control_window_height_),
      ImGuiChildFlags_Border, ImGuiWindowFlags_NoDecoration);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();

  ControlBar(props);
  props->control_bar_hovered_ = ImGui::IsWindowHovered();

  ImGui::EndChild();
  ImGui::End();
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor();

  return 0;
}