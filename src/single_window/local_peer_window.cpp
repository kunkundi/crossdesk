#include "IconsFontAwesome6.h"
#include "layout_style.h"
#include "localization.h"
#include "render.h"

int Render::LocalWindow() {
  ImGui::SetNextWindowPos(ImVec2(0, menu_window_height_), ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(255, 255, 255, 1));
  ImGui::BeginChild(
      "LocalDesktopWindow",
      ImVec2(local_window_width_,
             main_window_height_ - menu_window_height_ - status_bar_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::SetWindowFontScale(1.0f);
  ImGui::Text(
      "%s", localization::local_desktop[localization_language_index_].c_str());

  ImGui::Spacing();
  {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.05));
    ImGui::BeginChild(
        "LocalDesktopWindow_1", ImVec2(330, 180), ImGuiChildFlags_Border,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
      ImGui::SetWindowFontScale(0.5f);
      ImGui::Text("%s",
                  localization::local_id[localization_language_index_].c_str());

      ImGui::Spacing();

      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::InputText(
          "##local_id", (char *)mac_addr_str_.c_str(),
          mac_addr_str_.length() + 1,
          ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_ReadOnly);
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::SetWindowFontScale(0.5f);

      ImGui::Text("%s",
                  localization::password[localization_language_index_].c_str());

      strncpy(input_password_tmp_, input_password_, sizeof(input_password_));
      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
      ImGui::Spacing();
      ImGui::SetWindowFontScale(1.0f);
      ImGui::InputTextWithHint(
          "##server_pwd",
          localization::max_password_len[localization_language_index_].c_str(),
          input_password_, IM_ARRAYSIZE(input_password_),
          show_password_ ? ImGuiInputTextFlags_CharsNoBlank
                         : ImGuiInputTextFlags_CharsNoBlank |
                               ImGuiInputTextFlags_Password);

      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));

      ImGui::SetWindowFontScale(0.5f);
      if (ImGui::Button(show_password_ ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
                        ImVec2(35, 38))) {
        show_password_ = !show_password_;
      }
      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
      if (!regenerate_password_) {
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE, ImVec2(35, 38))) {
          regenerate_password_ = true;
        }
      } else {
        regenerate_password_frame_count_++;
        if (ImGui::Button(ICON_FA_SPINNER, ImVec2(35, 38))) {
        }
        if (regenerate_password_frame_count_ == 15) {
          regenerate_password_frame_count_ = 0;
          regenerate_password_ = false;
        }
      }
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleColor(3);

      if (strcmp(input_password_tmp_, input_password_)) {
        SaveSettingsIntoCacheFile();
      }
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();
  }

  ImGui::EndChild();
  ImGui::PopStyleColor();

  return 0;
}