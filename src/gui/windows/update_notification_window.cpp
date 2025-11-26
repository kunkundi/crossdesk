#include <algorithm>
#include <cstdlib>
#include <string>

#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

std::string CleanMarkdown(const std::string& markdown) {
  std::string result = markdown;

  // remove # title mark
  size_t pos = 0;
  while (pos < result.length()) {
    if (result[pos] == '\n' || pos == 0) {
      size_t line_start = (result[pos] == '\n') ? pos + 1 : pos;
      if (line_start < result.length() && result[line_start] == '#') {
        size_t hash_end = line_start;
        while (hash_end < result.length() &&
               (result[hash_end] == '#' || result[hash_end] == ' ')) {
          hash_end++;
        }

        result.erase(line_start, hash_end - line_start);
        pos = line_start;
        continue;
      }
    }
    pos++;
  }

  // remove ** bold mark
  pos = 0;
  while ((pos = result.find("**", pos)) != std::string::npos) {
    result.erase(pos, 2);
  }

  // remove all spaces
  result.erase(std::remove(result.begin(), result.end(), ' '), result.end());

  // replace . with 、
  pos = 0;
  while ((pos = result.find('.', pos)) != std::string::npos) {
    result.replace(pos, 1, "、");
    pos += 1;  // Move to next position after the replacement
  }

  return result;
}

int Render::UpdateNotificationWindow() {
  if (show_update_notification_window_ && update_available_) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    float window_width = update_notification_window_width_;
    float window_height = update_notification_window_height_;

#ifdef __APPLE__
    float font_scale = 0.3f;
#else
    float font_scale = 0.5f;
#endif

    ImGui::SetNextWindowPos(
        ImVec2(
            (viewport->WorkSize.x - viewport->WorkPos.x - window_width) / 2,
            (viewport->WorkSize.y - viewport->WorkPos.y - window_height) / 2),
        ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::Begin(
        localization::notification[localization_language_index_].c_str(),
        nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);

    if (system_chinese_font_ != nullptr) {
      ImGui::PushFont(system_chinese_font_);
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() +
                         5.0f);

    // title: new version available
    ImGui::SetCursorPosX(window_width * 0.1f);
    ImGui::SetWindowFontScale(font_scale + 0.2f);
    std::string title =
        localization::new_version_available[localization_language_index_] +
        ": v" + latest_version_;
    ImGui::Text("%s", title.c_str());
    ImGui::SetWindowFontScale(font_scale);

    ImGui::Spacing();

    // website link
    std::string download_text =
        localization::access_website[localization_language_index_] +
        "https://crossdesk.cn";
    Hyperlink(download_text, "https://crossdesk.cn", window_width);

    ImGui::Spacing();

    float scrollable_height =
        window_height - UPDATE_NOTIFICATION_RESERVED_HEIGHT;

    // scrollable content area
    ImGui::SetCursorPosX(window_width * 0.05f);
    ImGui::BeginChild("ScrollableContent",
                      ImVec2(window_width * 0.9f, scrollable_height),
                      ImGuiChildFlags_Border, ImGuiWindowFlags_None);

    // set text wrap position to current available width (accounts for
    // scrollbar)
    float wrap_pos = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(wrap_pos);

    // release name
    if (latest_version_info_.contains("releaseName") &&
        latest_version_info_["releaseName"].is_string() &&
        !latest_version_info_["releaseName"].empty()) {
      ImGui::SetCursorPosX(window_width * 0.05f);
      std::string release_name =
          latest_version_info_["releaseName"].get<std::string>();
      ImGui::TextWrapped("%s", release_name.c_str());
      ImGui::Spacing();
    }

    // release notes
    if (!release_notes_.empty()) {
      ImGui::SetCursorPosX(window_width * 0.05f);
      std::string cleaned_notes = CleanMarkdown(release_notes_);
      ImGui::TextWrapped("%s", cleaned_notes.c_str());
      ImGui::Spacing();
    }

    // release date
    if (latest_version_info_.contains("releaseDate") &&
        latest_version_info_["releaseDate"].is_string() &&
        !latest_version_info_["releaseDate"].empty()) {
      ImGui::SetCursorPosX(window_width * 0.05f);
      std::string date_label =
          localization::release_date[localization_language_index_];
      std::string release_date = latest_version_info_["releaseDate"];
      std::string date_text = date_label + release_date;
      ImGui::Text("%s", date_text.c_str());
      ImGui::Spacing();
    }

    // pop text wrap position
    ImGui::PopTextWrapPos();

    ImGui::EndChild();

    ImGui::Spacing();

    if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
      ImGui::SetCursorPosX(UPDATE_NOTIFICATION_OK_BUTTON_PADDING_CN);
    } else {
      ImGui::SetCursorPosX(UPDATE_NOTIFICATION_OK_BUTTON_PADDING_EN);
    }

    // update button
    if (ImGui::Button(
            localization::update[localization_language_index_].c_str())) {
      // open download page
      std::string url = "https://crossdesk.cn";
#if defined(_WIN32)
      std::string cmd = "start " + url;
#elif defined(__APPLE__)
      std::string cmd = "open " + url;
#else
      std::string cmd = "xdg-open " + url;
#endif
      system(cmd.c_str());
      show_update_notification_window_ = false;
    }

    ImGui::SameLine();

    if (ImGui::Button(
            localization::cancel[localization_language_index_].c_str())) {
      show_update_notification_window_ = false;
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetWindowFontScale(font_scale);

    // pop system font
    if (system_chinese_font_ != nullptr) {
      ImGui::PopFont();
    }

    ImGui::End();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
  }
  return 0;
}

}  // namespace crossdesk
