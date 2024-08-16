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

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  ImGui::SetNextWindowSize(
      ImVec2(control_window_width_, control_window_height_), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_), ImGuiCond_Once);
  if (ImGui::IsMouseReleased(ImGuiPopupFlags_MouseButtonLeft) ||
      control_window_width_is_changing_) {
    if (control_winodw_pos_.x <= main_window_width_ / 2) {
      int pos_x = 0;
      int pos_y = (control_winodw_pos_.y >= title_bar_height_ &&
                   control_winodw_pos_.y <=
                       main_window_height_ - control_window_height_)
                      ? control_winodw_pos_.y
                      : (control_winodw_pos_.y < title_bar_height_
                             ? title_bar_height_
                             : (main_window_height_ - control_window_height_));

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
    } else if (control_winodw_pos_.x > main_window_width_ / 2) {
      int pos_x = 0;
      int pos_y = (control_winodw_pos_.y >= title_bar_height_ &&
                   control_winodw_pos_.y <=
                       main_window_height_ - control_window_height_)
                      ? control_winodw_pos_.y
                      : (control_winodw_pos_.y < title_bar_height_
                             ? title_bar_height_
                             : (main_window_height_ - control_window_height_));

      if (control_bar_expand_) {
        if (control_window_width_ >= control_window_max_width_) {
          control_window_width_ = control_window_max_width_;
          control_window_width_is_changing_ = false;
          pos_x = main_window_width_ - control_window_max_width_;
        } else {
          control_window_width_is_changing_ = true;
          pos_x = main_window_width_ - control_window_width_;
        }
      } else {
        if (control_window_width_ <= control_window_min_width_) {
          control_window_width_ = control_window_min_width_;
          control_window_width_is_changing_ = false;
          pos_x = main_window_width_ - control_window_min_width_;
        } else {
          control_window_width_is_changing_ = true;
          pos_x = main_window_width_ - control_window_width_;
        }
      }
      ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
      is_control_bar_in_left_ = false;
    }
  }

  ImGui::Begin("ControlWindow", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();

  control_winodw_pos_ = ImGui::GetWindowPos();
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