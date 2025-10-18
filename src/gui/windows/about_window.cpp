#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::AboutWindow() {
  if (show_about_window_) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(
        (viewport->WorkSize.x - viewport->WorkPos.x - about_window_width_) / 2,
        (viewport->WorkSize.y - viewport->WorkPos.y - about_window_height_) /
            2));

    ImGui::SetNextWindowSize(ImVec2(about_window_width_, about_window_height_));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::SetWindowFontScale(0.5f);
    ImGui::Begin(
        localization::about[localization_language_index_].c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetWindowFontScale(0.5f);

    std::string version;
#ifdef RD_VERSION
    version = RD_VERSION;
#else
    version = "Unknown";
#endif

    std::string text =
        localization::version[localization_language_index_] + ": " + version;
    ImGui::Text("%s", text.c_str());

    ImGui::SetCursorPosX(about_window_width_ * 0.42f);
    ImGui::SetCursorPosY(about_window_height_ * 0.75f);
    // OK
    if (ImGui::Button(localization::ok[localization_language_index_].c_str())) {
      show_about_window_ = false;
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetWindowFontScale(0.5f);
    ImGui::End();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
  }
  return 0;
}