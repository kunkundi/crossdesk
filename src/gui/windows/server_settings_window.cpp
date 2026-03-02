#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

std::vector<std::string> GetRootEntries() {
  std::vector<std::string> roots;
#ifdef _WIN32
  DWORD mask = GetLogicalDrives();
  for (char letter = 'A'; letter <= 'Z'; ++letter) {
    if (mask & 1) {
      roots.push_back(std::string(1, letter) + ":\\");
    }
    mask >>= 1;
  }
#else
  roots.push_back("/");
#endif
  return roots;
}

int Render::SelfHostedServerWindow() {
  ImGuiIO& io = ImGui::GetIO();
  if (show_self_hosted_server_config_window_) {
    if (self_hosted_server_config_window_pos_reset_) {
      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.298f, io.DisplaySize.y * 0.25f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.407f, io.DisplaySize.y * 0.35f));
      } else {
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.27f, io.DisplaySize.y * 0.3f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.465f, io.DisplaySize.y * 0.35f));
      }

      self_hosted_server_config_window_pos_reset_ = false;
    }

    // Settings
    {
      ImGui::SetWindowFontScale(0.5f);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);

      ImGui::Begin(localization::self_hosted_server_settings
                       [localization_language_index_]
                           .c_str(),
                   nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetWindowFontScale(0.5f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", localization::self_hosted_server_address
                              [localization_language_index_]
                                  .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 2.5f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 3.43f);
        }
        ImGui::SetNextItemWidth(title_bar_button_width_ * 3.8f);

        ImGui::InputText("##signal_server_ip_self_", signal_server_ip_self_,
                         IM_ARRAYSIZE(signal_server_ip_self_),
                         ImGuiInputTextFlags_AlwaysOverwrite);
      }

      ImGui::Separator();

      {
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s",
            localization::self_hosted_server_port[localization_language_index_]
                .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 2.5f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 3.43f);
        }
        ImGui::SetNextItemWidth(title_bar_button_width_ * 3.8f);

        ImGui::InputText("##signal_server_port_self_", signal_server_port_self_,
                         IM_ARRAYSIZE(signal_server_port_self_));
      }

      ImGui::Separator();

      {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", localization::self_hosted_server_coturn_server_port
                              [localization_language_index_]
                                  .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 2.5f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 3.43f);
        }
        ImGui::SetNextItemWidth(title_bar_button_width_ * 3.8f);

        ImGui::InputText("##coturn_server_port_self_", coturn_server_port_self_,
                         IM_ARRAYSIZE(coturn_server_port_self_));
      }

      if (stream_window_inited_) {
        ImGui::EndDisabled();
      }

      ImGui::Dummy(ImVec2(0.0f, title_bar_button_width_ * 0.25f));

      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetCursorPosX(title_bar_button_width_ * 2.32f);
      } else {
        ImGui::SetCursorPosX(title_bar_button_width_ * 2.7f);
      }

      ImGui::PopStyleVar();

      // OK
      if (ImGui::Button(
              localization::ok[localization_language_index_].c_str())) {
        show_self_hosted_server_config_window_ = false;

        config_center_->SetServerHost(signal_server_ip_self_);
        config_center_->SetServerPort(atoi(signal_server_port_self_));
        config_center_->SetCoturnServerPort(atoi(coturn_server_port_self_));
        strncpy(signal_server_ip_, signal_server_ip_self_,
                sizeof(signal_server_ip_) - 1);
        signal_server_ip_[sizeof(signal_server_ip_) - 1] = '\0';
        strncpy(signal_server_port_, signal_server_port_self_,
                sizeof(signal_server_port_) - 1);
        signal_server_port_[sizeof(signal_server_port_) - 1] = '\0';
        strncpy(coturn_server_port_, coturn_server_port_self_,
                sizeof(coturn_server_port_) - 1);
        coturn_server_port_[sizeof(coturn_server_port_) - 1] = '\0';

        self_hosted_server_config_window_pos_reset_ = true;
      }

      ImGui::SameLine();
      // Cancel
      if (ImGui::Button(
              localization::cancel[localization_language_index_].c_str())) {
        show_self_hosted_server_config_window_ = false;
        self_hosted_server_config_window_pos_reset_ = true;
        strncpy(signal_server_ip_self_,
                config_center_->GetSignalServerHost().c_str(),
                sizeof(signal_server_ip_self_) - 1);
        signal_server_ip_self_[sizeof(signal_server_ip_self_) - 1] = '\0';
        int signal_port = config_center_->GetSignalServerPort();
        if (signal_port > 0) {
          strncpy(signal_server_port_self_, std::to_string(signal_port).c_str(),
                  sizeof(signal_server_port_self_) - 1);
          signal_server_port_self_[sizeof(signal_server_port_self_) - 1] = '\0';
        } else {
          signal_server_port_self_[0] = '\0';
        }
        int coturn_port = config_center_->GetCoturnServerPort();
        if (coturn_port > 0) {
          strncpy(coturn_server_port_self_, std::to_string(coturn_port).c_str(),
                  sizeof(coturn_server_port_self_) - 1);
          coturn_server_port_self_[sizeof(coturn_server_port_self_) - 1] = '\0';
        } else {
          coturn_server_port_self_[0] = '\0';
        }
      }

      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetWindowFontScale(0.5f);
      ImGui::End();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();
      ImGui::SetWindowFontScale(1.0f);
    }
  }

  return 0;
}
}  // namespace crossdesk