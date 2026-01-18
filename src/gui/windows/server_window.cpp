#include "render.h"

namespace crossdesk {
int Render::ServerWindow() {
  ImGui::SetNextWindowSize(
      ImVec2(main_window_width_ - 2 * main_child_window_x_padding_,
             main_window_height_ - status_bar_height_ -
                 main_window_text_y_padding_ - main_child_window_y_padding_),
      ImGuiCond_Always);
  ImGui::SetNextWindowPos(
      ImVec2(main_child_window_x_padding_, main_window_text_y_padding_),
      ImGuiCond_Always);
  ImGui::Begin("##server_window", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  // server window content goes here
  ImGui::Text("Server Window Content");
  ImGui::End();
  return 0;
}
}  // namespace crossdesk