#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::StreamWindow() {
  ImGui::SetNextWindowPos(
      ImVec2(0, fullscreen_button_pressed_ ? 0 : title_bar_height_),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(
      ImVec2(stream_window_width_, stream_window_height_ - title_bar_height_),
      ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 0));
  ImGui::PushStyleColor(ImGuiCol_Border,
                        ImVec4(178 / 255.0f, 178 / 255.0f, 178 / 255.0f,
                               fullscreen_button_pressed_ ? 0 : 1.0f));
  ImGui::Begin("VideoBg", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor(2);

  ControlWindow();

  ImGui::End();

  return 0;
}
