#include "render.h"

int Render::MainWindow() {
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(main_window_width_, main_window_height_),
                           ImGuiCond_Always);
  ImGui::Begin("Render", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
  MenuWindow();
  LocalWindow();
  RemoteWindow();

  ImGui::End();

  return 0;
}