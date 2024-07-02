#include "render.h"

int Render::MainWindow() {
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(main_window_width_, main_window_height_),
                           ImGuiCond_Always);

  MenuWindow();
  LocalWindow();
  RemoteWindow();
  StatusBar();
  ConnectionStatusWindow();

  return 0;
}