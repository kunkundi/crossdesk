#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

void Render::Hyperlink(const std::string& label, const std::string& url,
                       const float window_width) {
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 255, 255));
  ImGui::SetCursorPosX(window_width * 0.1f);
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
      int wide_len =
          MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
      if (wide_len > 0) {
        std::wstring wide_url(static_cast<size_t>(wide_len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wide_url[0],
                            wide_len);
        if (!wide_url.empty() && wide_url.back() == L'\0') {
          wide_url.pop_back();
        }

        std::wstring cmd = L"cmd.exe /c start \"\" \"" + wide_url + L"\"";
        STARTUPINFOW startup_info = {sizeof(startup_info)};
        PROCESS_INFORMATION process_info = {};
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup_info,
                           &process_info)) {
          CloseHandle(process_info.hThread);
          CloseHandle(process_info.hProcess);
        }
      }
#elif defined(__APPLE__)
      std::string cmd = "open " + url;
#else
      std::string cmd = "xdg-open " + url;
#endif
#if !defined(_WIN32)
      system(cmd.c_str());  // open browser
#endif
    }
  }
}

int Render::AboutWindow() {
  if (show_about_window_) {
    float about_window_width = title_bar_button_width_ * 7.5f;
    float about_window_height = latest_version_.empty()
                                    ? title_bar_button_width_ * 4.0f
                                    : title_bar_button_width_ * 4.6f;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(
        (viewport->WorkSize.x - viewport->WorkPos.x - about_window_width) / 2,
        (viewport->WorkSize.y - viewport->WorkPos.y - about_window_height) /
            2));

    ImGui::SetNextWindowSize(ImVec2(about_window_width, about_window_height));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);
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
                       ": CrossDesk " + version;
    ImGui::SetCursorPosX(about_window_width * 0.1f);
    ImGui::Text("%s", text.c_str());

    if (update_available_) {
      std::string latest_version =
          localization::new_version_available[localization_language_index_] +
          ": " + latest_version_;
      std::string access_website =
          localization::access_website[localization_language_index_];
      Hyperlink(latest_version, "https://crossdesk.cn", about_window_width);
    }

    ImGui::Text("");

    std::string copyright_text = "© 2025 by JUNKUN DI. All rights reserved.";
    std::string license_text = "Licensed under GNU LGPL v3.";
    ImGui::SetCursorPosX(about_window_width * 0.1f);
    ImGui::Text("%s", copyright_text.c_str());
    ImGui::SetCursorPosX(about_window_width * 0.1f);
    ImGui::Text("%s", license_text.c_str());

    ImGui::SetCursorPosX(about_window_width * 0.445f);
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