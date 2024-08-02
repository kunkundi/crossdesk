#include "render.h"

int Render::ControlWindow() {
  auto time_duration = ImGui::GetTime() - control_bar_button_pressed_time_;
  auto control_window_width =
      !control_bar_button_pressed_
          ? time_duration < 0.25f
                ? control_window_max_width_ -
                      (control_window_max_width_ - control_window_min_width_) *
                          4 * time_duration
                : control_window_min_width_
      : time_duration < 0.25f
          ? control_window_min_width_ +
                (control_window_max_width_ - control_window_min_width_) * 4 *
                    time_duration
          : control_window_max_width_;

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  //   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  //   ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0);
  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_), ImGuiCond_Once);
  ImGui::SetNextWindowSize(
      ImVec2(control_window_width + 10, control_window_height_ + 10),
      ImGuiCond_Always);
  ImGui::Begin("ControlWindow", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  static bool a, b, c, d, e;
  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_), ImGuiCond_Once);
  ImGui::SetWindowFontScale(0.5f);

  ImGui::BeginChild("ControlBar",
                    ImVec2(control_window_width, control_window_height_),
                    ImGuiChildFlags_Border, ImGuiWindowFlags_NoDecoration);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();

  ControlBar();

  ImGui::EndChild();

  ImGui::End();
  //   ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  return 0;
}