#include <random>

#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

int Render::LocalWindow() {
  ImGuiIO& io = ImGui::GetIO();
  float local_window_width = io.DisplaySize.x * 0.5f;
  float local_window_height =
      io.DisplaySize.y * (1 - TITLE_BAR_HEIGHT - STATUS_BAR_HEIGHT);
  float local_window_button_width = io.DisplaySize.x * 0.046f;
  float local_window_button_height = io.DisplaySize.y * 0.075f;

  ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y * TITLE_BAR_HEIGHT),
                          ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("LocalDesktopWindow",
                    ImVec2(local_window_width, local_window_height),
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  ImGui::SetCursorPos(
      ImVec2(io.DisplaySize.x * 0.045f, io.DisplaySize.y * 0.02f));

  ImGui::SetWindowFontScale(0.9f);
  ImGui::TextColored(
      ImVec4(0.0f, 0.0f, 0.0f, 0.5f), "%s",
      localization::local_desktop[localization_language_index_].c_str());

  ImGui::Spacing();
  {
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.045f, io.DisplaySize.y * 0.15f),
        ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(239.0f / 255, 240.0f / 255,
                                                   242.0f / 255, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::BeginChild(
        "LocalDesktopPanel",
        ImVec2(local_window_width * 0.8f, local_window_height * 0.43f),
        ImGuiChildFlags_Border,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
      ImGui::SetWindowFontScale(0.8f);
      ImGui::Text("%s",
                  localization::local_id[localization_language_index_].c_str());

      ImGui::Spacing();

      ImGui::SetNextItemWidth(io.DisplaySize.x * 0.25f);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

      if (strcmp(client_id_display_, client_id_)) {
        for (int i = 0, j = 0; i < sizeof(client_id_); i++, j++) {
          client_id_display_[j] = client_id_[i];
          if (i == 2 || i == 5) {
            client_id_display_[++j] = ' ';
          }
        }
      }

      ImGui::InputText(
          "##local_id", client_id_display_, IM_ARRAYSIZE(client_id_display_),
          ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_ReadOnly);
      ImGui::PopStyleVar();

      ImGui::SameLine();

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
      ImGui::SetWindowFontScale(0.5f);
      if (ImGui::Button(ICON_FA_COPY, ImVec2(local_window_button_width,
                                             local_window_button_height))) {
        local_id_copied_ = true;
        ImGui::SetClipboardText(client_id_);
        copy_start_time_ = ImGui::GetTime();
      }
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleColor(3);

      double time_duration = ImGui::GetTime() - copy_start_time_;
      if (local_id_copied_ && time_duration < 1.0f) {
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f - (float)time_duration));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::Begin("ConnectionStatusWindow", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        auto window_width = ImGui::GetWindowSize().x;
        auto window_height = ImGui::GetWindowSize().y;
        ImGui::SetWindowFontScale(0.8f);
        std::string text = localization::local_id_copied_to_clipboard
            [localization_language_index_];
        auto text_width = ImGui::CalcTextSize(text.c_str()).x;
        ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
        ImGui::SetCursorPosY(window_height * 0.4f);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0, 0, 0, 1.0f - (float)time_duration));
        ImGui::Text("%s", text.c_str());
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::End();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::SetWindowFontScale(0.8f);
      ImGui::Text("%s",
                  localization::password[localization_language_index_].c_str());
      ImGui::SetWindowFontScale(1.0f);

      ImGui::SetNextItemWidth(io.DisplaySize.x * 0.25f);
      ImGui::Spacing();

      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::InputTextWithHint(
          "##server_pwd",
          localization::max_password_len[localization_language_index_].c_str(),
          password_saved_, IM_ARRAYSIZE(password_saved_),
          show_password_
              ? ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_ReadOnly
              : ImGuiInputTextFlags_CharsNoBlank |
                    ImGuiInputTextFlags_Password |
                    ImGuiInputTextFlags_ReadOnly);
      ImGui::PopStyleVar();

      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
      ImGui::SetWindowFontScale(0.5f);
      auto l_x = ImGui::GetCursorScreenPos().x;
      auto l_y = ImGui::GetCursorScreenPos().y;
      if (ImGui::Button(
              show_password_ ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
              ImVec2(local_window_button_width, local_window_button_height))) {
        show_password_ = !show_password_;
      }

      ImGui::SameLine();

      if (ImGui::Button(ICON_FA_PEN, ImVec2(local_window_button_width,
                                            local_window_button_height))) {
        show_reset_password_window_ = true;
      }
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleColor(3);

      if (show_reset_password_window_) {
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0, 1.0, 1.0, 1.0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);

        ImGui::Begin("ResetPasswordWindow", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        auto window_width = ImGui::GetWindowSize().x;
        auto window_height = ImGui::GetWindowSize().y;
        std::string text =
            localization::new_password[localization_language_index_];
        ImGui::SetWindowFontScale(0.5f);
        auto text_width = ImGui::CalcTextSize(text.c_str()).x;
        ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
        ImGui::SetCursorPosY(window_height * 0.2f);
        ImGui::Text("%s", text.c_str());

        ImGui::SetCursorPosX(window_width * 0.33f);
        ImGui::SetCursorPosY(window_height * 0.4f);
        ImGui::SetNextItemWidth(window_width * 0.33f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        if (focus_on_input_widget_) {
          ImGui::SetKeyboardFocusHere();
          focus_on_input_widget_ = false;
        }

        bool enter_pressed = ImGui::InputText(
            "##new_password", new_password_, IM_ARRAYSIZE(new_password_),
            ImGuiInputTextFlags_CharsNoBlank |
                ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::PopStyleVar();

        ImGui::SetCursorPosX(window_width * 0.315f);
        ImGui::SetCursorPosY(window_height * 0.75f);

        // OK
        if (ImGui::Button(
                localization::ok[localization_language_index_].c_str()) ||
            enter_pressed) {
          if (6 != strlen(new_password_)) {
            LOG_ERROR("Invalid password length");
            show_reset_password_window_ = true;
            focus_on_input_widget_ = true;
          } else {
            show_reset_password_window_ = false;
            memset(&password_saved_, 0, sizeof(password_saved_));
            strncpy(password_saved_, new_password_,
                    sizeof(password_saved_) - 1);
            password_saved_[sizeof(password_saved_) - 1] = '\0';

            // if self hosted
            if (config_center_->IsSelfHosted()) {
              std::string self_hosted_id_str;
              if (strlen(self_hosted_id_) > 0) {
                const char* at_pos = strchr(self_hosted_id_, '@');
                if (at_pos != nullptr) {
                  self_hosted_id_str =
                      std::string(self_hosted_id_, at_pos - self_hosted_id_);
                } else {
                  self_hosted_id_str = self_hosted_id_;
                }
              } else {
                self_hosted_id_str = client_id_;
              }

              std::string new_self_hosted_id =
                  self_hosted_id_str + "@" + password_saved_;
              memset(&self_hosted_id_, 0, sizeof(self_hosted_id_));
              strncpy(self_hosted_id_, new_self_hosted_id.c_str(),
                      sizeof(self_hosted_id_) - 1);
              self_hosted_id_[sizeof(self_hosted_id_) - 1] = '\0';

            } else {
              std::string client_id_with_password =
                  std::string(client_id_) + "@" + password_saved_;
              strncpy(client_id_with_password_, client_id_with_password.c_str(),
                      sizeof(client_id_with_password_) - 1);
              client_id_with_password_[sizeof(client_id_with_password_) - 1] =
                  '\0';
            }

            SaveSettingsIntoCacheFile();

            memset(new_password_, 0, sizeof(new_password_));

            LeaveConnection(peer_, client_id_);
            DestroyPeer(&peer_);
            focus_on_input_widget_ = true;
          }
        }

        ImGui::SameLine();

        if (ImGui::Button(
                localization::cancel[localization_language_index_].c_str())) {
          show_reset_password_window_ = false;
          focus_on_input_widget_ = true;
          memset(new_password_, 0, sizeof(new_password_));
        }

        ImGui::SetWindowFontScale(1.0f);

        ImGui::End();
        ImGui::PopStyleVar();
      }
    }

    ImGui::EndChild();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();

  return 0;
}
}  // namespace crossdesk