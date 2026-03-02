#include "rd_log.h"
#include "render.h"

namespace crossdesk {

int Render::ControlWindow(std::shared_ptr<SubStreamWindowProperties>& props) {
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

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_ * 1.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, window_rounding_ * 1.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  float y_boundary = fullscreen_button_pressed_ ? 0.0f : title_bar_height_;
  float container_x = 0.0f;
  float container_y = y_boundary;
  float container_w = stream_window_width_;
  float container_h = stream_window_height_ - y_boundary;

  ImGui::SetNextWindowSize(ImVec2(container_w, container_h), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(container_x, container_y), ImGuiCond_Always);

  float pos_x = 0;
  float pos_y = 0;

  std::string container_window_title =
      props->remote_id_ + "ControlContainerWindow";
  ImGui::Begin(container_window_title.c_str(), nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoBackground);
  ImGui::PopStyleVar();

  ImVec2 container_pos = ImGui::GetWindowPos();

  if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      props->control_bar_hovered_) {
    float current_x_rel = props->control_window_pos_.x - container_pos.x;
    float current_y_rel = props->control_window_pos_.y - container_pos.y;
    ImVec2 delta = ImGui::GetIO().MouseDelta;
    pos_x = current_x_rel + delta.x;
    pos_y = current_y_rel + delta.y;
    if (pos_x < 0.0f) pos_x = 0.0f;
    if (pos_y < 0.0f) pos_y = 0.0f;
    if (pos_x + props->control_window_width_ > container_w)
      pos_x = container_w - props->control_window_width_;
    if (pos_y + props->control_window_height_ > container_h)
      pos_y = container_h - props->control_window_height_;
  } else if (props->reset_control_bar_pos_) {
    float new_cursor_pos_x = 0.0f;
    float new_cursor_pos_y = 0.0f;

    // set control window pos
    float current_y_rel = props->control_window_pos_.y - container_pos.y;
    if (current_y_rel + props->control_window_height_ > container_h) {
      pos_y = container_h - props->control_window_height_;
    } else if (current_y_rel < 0.0f) {
      pos_y = 0.0f;
    } else {
      pos_y = current_y_rel;
    }

    if (props->is_control_bar_in_left_) {
      pos_x = 0.0f;
    } else {
      pos_x = container_w - props->control_window_width_;
    }

    if (0 != props->mouse_diff_control_bar_pos_x_ &&
        0 != props->mouse_diff_control_bar_pos_y_) {
      // set cursor pos
      new_cursor_pos_x =
          container_pos.x + pos_x + props->mouse_diff_control_bar_pos_x_;
      new_cursor_pos_y =
          container_pos.y + pos_y + props->mouse_diff_control_bar_pos_y_;

      SDL_WarpMouseInWindow(stream_window_, (int)new_cursor_pos_x,
                            (int)new_cursor_pos_y);
    }
    props->reset_control_bar_pos_ = false;
  } else if (!props->reset_control_bar_pos_ &&
                 ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
             props->control_window_width_is_changing_) {
    float current_x_rel = props->control_window_pos_.x - container_pos.x;
    float current_y_rel = props->control_window_pos_.y - container_pos.y;
    if (current_x_rel <= container_w * 0.5f) {
      pos_x = 0.0f;
      if (current_y_rel + props->control_window_height_ > container_h) {
        pos_y = container_h - props->control_window_height_;
      } else {
        pos_y = current_y_rel;
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
    } else if (current_x_rel > container_w * 0.5f) {
      pos_x = container_w - props->control_window_width_;
      pos_y = (current_y_rel >= 0.0f &&
               current_y_rel <= container_h - props->control_window_height_)
                  ? current_y_rel
                  : (current_y_rel < 0.0f
                         ? 0.0f
                         : (container_h - props->control_window_height_));

      if (props->control_bar_expand_) {
        if (props->control_window_width_ >= props->control_window_max_width_) {
          props->control_window_width_ = props->control_window_max_width_;
          props->control_window_width_is_changing_ = false;
          pos_x = container_w - props->control_window_max_width_;
        } else {
          props->control_window_width_is_changing_ = true;
          pos_x = container_w - props->control_window_width_;
        }
      } else {
        if (props->control_window_width_ <= props->control_window_min_width_) {
          props->control_window_width_ = props->control_window_min_width_;
          props->control_window_width_is_changing_ = false;
          pos_x = container_w - props->control_window_min_width_;
        } else {
          props->control_window_width_is_changing_ = true;
          pos_x = container_w - props->control_window_width_;
        }
      }
      props->is_control_bar_in_left_ = false;
    }

    if (current_y_rel + props->control_window_height_ > container_h) {
      pos_y = container_h - props->control_window_height_;
    } else if (current_y_rel < 0.0f) {
      pos_y = 0.0f;
    }
  } else {
    float current_x_rel = props->control_window_pos_.x - container_pos.x;
    float current_y_rel = props->control_window_pos_.y - container_pos.y;
    pos_x = current_x_rel;
    pos_y = current_y_rel;
    if (pos_x < 0.0f) pos_x = 0.0f;
    if (pos_y < 0.0f) pos_y = 0.0f;
    if (pos_x + props->control_window_width_ > container_w)
      pos_x = container_w - props->control_window_width_;
    if (pos_y + props->control_window_height_ > container_h)
      pos_y = container_h - props->control_window_height_;
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

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  static bool a, b, c, d, e;
  float child_cursor_x = pos_x;
  float child_cursor_y = pos_y;
  ImGui::SetCursorPos(ImVec2(child_cursor_x, child_cursor_y));

  std::string control_child_window_title =
      props->remote_id_ + "ControlChildWindow";
  ImGui::BeginChild(
      control_child_window_title.c_str(),
      ImVec2(props->control_window_width_, props->control_window_height_),
      ImGuiChildFlags_Border, ImGuiWindowFlags_NoDecoration);
  ImGui::PopStyleColor();

  props->control_window_pos_ = ImGui::GetWindowPos();
  SDL_GetMouseState(&props->mouse_pos_x_, &props->mouse_pos_y_);
  props->mouse_diff_control_bar_pos_x_ =
      props->mouse_pos_x_ - props->control_window_pos_.x;
  props->mouse_diff_control_bar_pos_y_ =
      props->mouse_pos_y_ - props->control_window_pos_.y;

  if (props->control_window_pos_.y < container_pos.y ||
      props->control_window_pos_.y + props->control_window_height_ >
          (container_pos.y + container_h) ||
      props->control_window_pos_.x < container_pos.x ||
      props->control_window_pos_.x + props->control_window_width_ >
          (container_pos.x + container_w)) {
    ImGui::ClearActiveID();
    props->reset_control_bar_pos_ = true;
    props->mouse_diff_control_bar_pos_x_ = 0;
    props->mouse_diff_control_bar_pos_y_ = 0;
  }

  ControlBar(props);
  props->control_bar_hovered_ = ImGui::IsWindowHovered();

  ImGui::EndChild();
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();

  return 0;
}
}  // namespace crossdesk
