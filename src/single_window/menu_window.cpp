#include "IconsFontAwesome6.h"
#include "log.h"
#include "render.h"

int Render::MenuWindow() {
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(255, 255, 255, 1));
  static bool a, b, c, d, e;
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetWindowFontScale(0.8f);
  ImGui::BeginChild(
      "MenuWindow", ImVec2(main_window_width_, menu_window_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::SetWindowFontScale(1.0f);
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu(ICON_FA_BARS, true)) {
      ImGui::SetWindowFontScale(0.5f);
      ImGui::MenuItem("Auto-resizing window", NULL, &a);
      ImGui::MenuItem("Constrained-resizing window", NULL, &b);
      ImGui::MenuItem("Fullscreen window", NULL, &c);
      ImGui::MenuItem("Long text display", NULL, &d);
      ImGui::MenuItem("Manipulating window titles", NULL, &e);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::EndMenu();
    }

    ImGui::Dummy(ImVec2(main_window_width_ - 97, 0));

    SettingButton();
  }

  ImGui::PopStyleColor();
  ImGui::EndChild();

  return 0;
}