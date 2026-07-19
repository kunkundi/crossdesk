#include "application/gui_application.h"
#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"

namespace crossdesk {

static int InputTextCallback(ImGuiInputTextCallbackData *data);

int GuiApplication::RemoteWindow() {
  ImGuiIO &io = ImGui::GetIO();
  float remote_window_width = io.DisplaySize.x * 0.5f;
  float remote_window_height =
      io.DisplaySize.y * (1 - TITLE_BAR_HEIGHT - STATUS_BAR_HEIGHT);
  float remote_window_arrow_button_width = io.DisplaySize.x * 0.1f;
  float remote_window_arrow_button_height = io.DisplaySize.y * 0.078f;

  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * TITLE_BAR_HEIGHT),
      ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("RemoteDesktopWindow",
                    ImVec2(remote_window_width, remote_window_height),
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  ImGui::SetCursorPos(
      ImVec2(io.DisplaySize.x * 0.057f, io.DisplaySize.y * 0.02f));

  ImGui::SetWindowFontScale(0.9f);
  ImGui::TextColored(
      ImVec4(0.0f, 0.0f, 0.0f, 0.5f), "%s",
      localization::remote_desktop[localization_language_index_].c_str());

  ImGui::Spacing();
  {
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.557f, io.DisplaySize.y * 0.15f),
        ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(239.0f / 255, 240.0f / 255,
                                                   242.0f / 255, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);

    ImGui::BeginChild(
        "RemoteDesktopWindow_1",
        ImVec2(remote_window_width * 0.8f, remote_window_height * 0.43f),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
      ImGui::SetWindowFontScale(0.8f);
      ImGui::Text(
          "%s", localization::remote_id[localization_language_index_].c_str());

      ImGui::Spacing();
      ImGui::SetNextItemWidth(io.DisplaySize.x * 0.25f);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      if (re_enter_remote_id_) {
        ImGui::SetKeyboardFocusHere();
        re_enter_remote_id_ = false;
        memset(remote_id_display_, 0, sizeof(remote_id_display_));
      }
      bool enter_pressed = ImGui::InputText(
          "##remote_id_", remote_id_display_, IM_ARRAYSIZE(remote_id_display_),
          ImGuiInputTextFlags_CharsDecimal |
              ImGuiInputTextFlags_EnterReturnsTrue |
              ImGuiInputTextFlags_CallbackEdit,
          InputTextCallback);

      ImGui::PopStyleVar();
      ImGui::SameLine();

      std::string remote_id = remote_id_display_;
      remote_id.erase(remove_if(remote_id.begin(), remote_id.end(),
                                static_cast<int (*)(int)>(&isspace)),
                      remote_id.end());
      if (ImGui::Button(ICON_FA_ARROW_RIGHT_LONG,
                        ImVec2(remote_window_arrow_button_width,
                               remote_window_arrow_button_height)) ||
          enter_pressed) {
        connect_button_pressed_ = true;
        bool found = false;
        std::string target_remote_id;
        std::string target_password;
        bool should_connect = false;
        bool already_connected = false;

        for (auto &[id, props] : recent_connections_) {
          if (id.find(remote_id) != std::string::npos) {
            found = true;
            target_remote_id = props.remote_id;
            target_password = props.password;
            {
              // std::shared_lock lock(remote_sessions_mutex_);
              if (remote_sessions_.find(remote_id) !=
                  remote_sessions_.end()) {
                if (!remote_sessions_[remote_id]->connection_established_) {
                  should_connect = true;
                } else {
                  already_connected = true;
                }
              } else {
                should_connect = true;
              }
            }

            if (should_connect) {
              ConnectTo(target_remote_id, target_password.c_str(), false);
            } else if (already_connected) {
              LOG_INFO("Already connected to [{}]", remote_id);
            }
            break;
          }
        }
        if (!found) {
          ConnectTo(remote_id, "", false);
        }
      }

      // check every 1 second for rejoin
      if (need_to_rejoin_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_rejoin_check_time_)
                           .count();

        if (elapsed >= 1000) {
          last_rejoin_check_time_ = now;
          need_to_rejoin_ = false;
          // std::shared_lock lock(remote_sessions_mutex_);
          for (const auto &[_, props] : remote_sessions_) {
            if (props->rejoin_) {
              ConnectTo(props->remote_id_, props->remote_password_,
                        props->remember_password_);
            }
          }
        }
      }
    }
    ImGui::EndChild();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();

  return 0;
}

static int InputTextCallback(ImGuiInputTextCallbackData *data) {
  if (data->BufTextLen > 3 && data->Buf[3] != ' ') {
    data->InsertChars(3, " ");
  }

  if (data->BufTextLen > 7 && data->Buf[7] != ' ') {
    data->InsertChars(7, " ");
  }

  return 0;
}

} // namespace crossdesk
