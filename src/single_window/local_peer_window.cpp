#include <random>

#include "IconsFontAwesome6.h"
#include "layout_style.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::LocalWindow() {
  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild(
      "LocalDesktopWindow",
      ImVec2(local_window_width_, main_window_height_default_ -
                                      title_bar_height_ - status_bar_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  ImGui::SetWindowFontScale(1.0f);
  ImGui::Text(
      "%s", localization::local_desktop[localization_language_index_].c_str());

  ImGui::Spacing();
  {
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImVec4(239.0 / 255, 240.0 / 255, 242.0 / 255, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::BeginChild("LocalDesktopWindow_1", ImVec2(330, 180),
                      ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
      ImGui::SetWindowFontScale(0.5f);
      ImGui::Text("%s",
                  localization::local_id[localization_language_index_].c_str());

      ImGui::Spacing();

      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

      std::string client_id_show = "";
      for (int i = 0; i < sizeof(client_id_); i++) {
        client_id_show += client_id_[i];
        if (i == 2 || i == 5) {
          client_id_show += " ";
        }
      }

      ImGui::InputText(
          "##local_id", (char *)client_id_show.c_str(), sizeof(client_id_show),
          ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_ReadOnly);
      ImGui::PopStyleVar();

      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
      ImGui::SetWindowFontScale(0.5f);

      if (ImGui::Button(ICON_FA_COPY, ImVec2(35, 38))) {
        local_id_copied_ = true;
        ImGui::SetClipboardText(client_id_);
        copy_start_time_ = ImGui::GetTime();
      }

      ImGui::PopStyleColor(3);

      auto time_duration = ImGui::GetTime() - copy_start_time_;
      if (local_id_copied_ && time_duration < 1.0f) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                    notification_window_width_) /
                       2,
                   (viewport->WorkSize.y - viewport->WorkPos.y -
                    notification_window_height_) /
                       2));

        ImGui::SetNextWindowSize(
            ImVec2(notification_window_width_, notification_window_height_));
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              ImVec4(1.0, 1.0, 1.0, 1.0 - time_duration));
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
                              ImVec4(0, 0, 0, 1.0 - time_duration));
        ImGui::Text("%s", text.c_str());
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::End();
      }

      ImGui::SetWindowFontScale(1.0f);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::SetWindowFontScale(0.5f);

      ImGui::Text("%s",
                  localization::password[localization_language_index_].c_str());

      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
      ImGui::Spacing();

      if (!password_inited_) {
        char a[] = {
            "1234567890QWERTYUIOPASDFGHJKLZXCVBNMqwertyuiopasdfghjklzxcvbnm"};
        std::mt19937 generator(
            std::chrono::system_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<int> distribution(0, strlen(a) - 1);

        random_password_.clear();
        for (int i = 0, len = strlen(a); i < 6; i++) {
          random_password_ += a[distribution(generator)];
        }
        password_inited_ = true;
        if (random_password_ != password_saved_) {
          password_saved_ = random_password_;
          SaveSettingsIntoCacheFile();
        }
      }

      ImGui::SetWindowFontScale(1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::InputTextWithHint(
          "##server_pwd",
          localization::max_password_len[localization_language_index_].c_str(),
          (char *)random_password_.c_str(), random_password_.length() + 1,
          show_password_ ? ImGuiInputTextFlags_CharsNoBlank
                         : ImGuiInputTextFlags_CharsNoBlank |
                               ImGuiInputTextFlags_Password);
      ImGui::PopStyleVar();

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

      if (ImGui::Button(
              regenerate_password_ ? ICON_FA_SPINNER : ICON_FA_ARROWS_ROTATE,
              ImVec2(35, 38))) {
        regenerate_password_ = true;
        password_inited_ = false;
        regenerate_password_start_time_ = ImGui::GetTime();
        LeaveConnection(peer_, client_id_);
        is_create_connection_ = false;
      }
      if (ImGui::GetTime() - regenerate_password_start_time_ > 0.3f) {
        regenerate_password_ = false;
      }

      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleColor(3);
    }

    ImGui::EndChild();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();

  return 0;
}