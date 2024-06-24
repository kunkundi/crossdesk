#include "render.h"

int Render::MenuWindow() {
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0, 0, 0, 0));
  static bool a, b, c, d, e;
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::BeginChild("MenuWindow",
                    ImVec2(main_window_width_, menu_window_height_),
                    ImGuiChildFlags_Border);

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("Menu")) {
      ImGui::SetWindowFontScale(0.5f);
      ImGui::MenuItem("Auto-resizing window", NULL, &a);
      ImGui::MenuItem("Constrained-resizing window", NULL, &b);
      ImGui::MenuItem("Fullscreen window", NULL, &c);
      ImGui::MenuItem("Long text display", NULL, &d);
      ImGui::MenuItem("Manipulating window titles", NULL, &e);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
  SettingWindow();

  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::EndChild();

  return 0;
}