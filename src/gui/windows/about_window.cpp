#include <cstdlib>
#include <string>

#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

void Hyperlink(const std::string& label, const std::string& url) {
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 255, 255));
  ImGui::Text("%s", label.c_str());
  ImGui::PopStyleColor();

  if (ImGui::IsItemHovered()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::BeginTooltip();
    ImGui::SetWindowFontScale(0.6f);
    ImGui::TextUnformatted(url.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndTooltip();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
#if defined(_WIN32)
      std::string cmd = "start " + url;
#elif defined(__APPLE__)
      std::string cmd = "open " + url;
#else
      std::string cmd = "xdg-open " + url;
#endif
      system(cmd.c_str());  // open browser
    }
  }
}

int Render::AboutWindow() {
  if (show_about_window_) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    float about_window_height = latest_version_.empty()
                                    ? about_window_height_
                                    : about_window_height_ + 20.0f;
    ImGui::SetNextWindowPos(ImVec2(
        (viewport->WorkSize.x - viewport->WorkPos.x - about_window_width_) / 2,
        (viewport->WorkSize.y - viewport->WorkPos.y - about_window_height) /
            2));

    ImGui::SetNextWindowSize(ImVec2(about_window_width_, about_window_height));

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
#ifdef CROSSDESK_VERSION
    version = CROSSDESK_VERSION;
#else
    version = "Unknown";
#endif

    std::string text = localization::version[localization_language_index_] +
                       ": CrossDesk v" + version;
    ImGui::Text("%s", text.c_str());

    if (update_available_) {
      std::string latest_version =
          localization::new_version_available[localization_language_index_] +
          ": " + latest_version_;
      std::string access_website =
          localization::access_website[localization_language_index_];
      Hyperlink(latest_version, "https://crossdesk.cn");
    }

    ImGui::Text("");

    std::string copyright_text = "© 2025 by JUNKUN DI. All rights reserved.";
    std::string license_text = "Licensed under GNU LGPL v3.";
    ImGui::Text("%s", copyright_text.c_str());
    ImGui::Text("%s", license_text.c_str());

    ImGui::SetCursorPosX(about_window_width_ * 0.42f);
    ImGui::SetCursorPosY(about_window_height * 0.75f);
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
}  // namespace crossdesk