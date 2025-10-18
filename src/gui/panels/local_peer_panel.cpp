#include <random>

#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::LocalWindow() {
  ImGui::SetNextWindowPos(ImVec2(-1.0f, title_bar_height_), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild("LocalDesktopWindow",
                    ImVec2(local_window_width_, local_window_height_),
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + main_window_text_y_padding_);
  ImGui::Indent(main_child_window_x_padding_);

  ImGui::TextColored(
      ImVec4(0.0f, 0.0f, 0.0f, 0.5f), "%s",
      localization::local_desktop[localization_language_index_].c_str());

  ImGui::Spacing();
  {
    ImGui::SetNextWindowPos(
        ImVec2(main_child_window_x_padding_,
               title_bar_height_ + main_child_window_y_padding_),
        ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(239.0f / 255, 240.0f / 255,
                                                   242.0f / 255, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::BeginChild(
        "LocalDesktopWindow_1",
        ImVec2(local_child_window_width_, local_child_window_height_),
        ImGuiChildFlags_Border,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
      ImGui::SetWindowFontScale(0.8f);
      ImGui::Text("%s",
                  localization::local_id[localization_language_index_].c_str());

      ImGui::Spacing();

      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
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
      if (ImGui::Button(ICON_FA_COPY, ImVec2(22, 38))) {
        local_id_copied_ = true;
        ImGui::SetClipboardText(client_id_);
        copy_start_time_ = ImGui::GetTime();
      }
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleColor(3);

      double time_duration = ImGui::GetTime() - copy_start_time_;
      if (local_id_copied_ && time_duration < 1.0f) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                    notification_window_width_) /
                       2,
                   (viewport->WorkSize.y - viewport->WorkPos.y -
                    notification_window_height_) /
                       2));

        ImGui::SetNextWindowSize(
            ImVec2(notification_window_width_, notification_window_height_));
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
        ImGui::SetCursorPosY(window_height * 0.5f);
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

      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
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
      if (ImGui::Button(ICON_FA_EYE, ImVec2(22, 38))) {
        show_password_ = !show_password_;
      }

      if (!show_password_) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(ImVec2(l_x + 3.0f, l_y + 12.5f),
                           ImVec2(l_x + 20.3f, l_y + 26.5f),
                           IM_COL32(239, 240, 242, 255), 2.0f);
        draw_list->AddLine(ImVec2(l_x + 3.0f, l_y + 11.0f),
                           ImVec2(l_x + 20.3f, l_y + 25.0f),
                           IM_COL32(0, 0, 0, 255), 1.5f);
      }

      ImGui::SameLine();

      if (ImGui::Button(ICON_FA_PEN, ImVec2(22, 38))) {
        show_reset_password_window_ = true;
      }
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleColor(3);

      if (show_reset_password_window_) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(
            ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                    connection_status_window_width_) /
                       2,
                   (viewport->WorkSize.y - viewport->WorkPos.y -
                    connection_status_window_height_) /
                       2));

        ImGui::SetNextWindowSize(ImVec2(connection_status_window_width_,
                                        connection_status_window_height_));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0, 1.0, 1.0, 1.0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);

        ImGui::Begin("ResetPasswordWindow", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
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

        ImGui::SetCursorPosX((window_width - IPUT_WINDOW_WIDTH / 2) * 0.5f);
        ImGui::SetCursorPosY(window_height * 0.4f);
        ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH / 2);

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

            std::string client_id_with_password =
                std::string(client_id_) + "@" + password_saved_;
            strncpy(client_id_with_password_, client_id_with_password.c_str(),
                    sizeof(client_id_with_password_) - 1);
            client_id_with_password_[sizeof(client_id_with_password_) - 1] =
                '\0';

            SaveSettingsIntoCacheFile();

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