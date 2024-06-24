#include "IconsFontAwesome6.h"
#include "layout_style.h"
#include "localization.h"
#include "log.h"
#include "render.h"

int Render::RemoteWindow() {
  // if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
  //   ImGui::SetNextWindowSize(
  //       ImVec2(main_window_width_ - MENU_WINDOW_WIDTH_CN,
  //              main_window_height_ - menu_window_height_));
  // } else {
  //   ImGui::SetNextWindowSize(
  //       ImVec2(MENU_WINDOW_WIDTH_EN, MENU_WINDOW_HEIGHT_EN));
  // }

  ImGui::SetNextWindowPos(ImVec2(local_window_width_, menu_window_height_),
                          ImGuiCond_Always);

  ImGui::BeginChild(
      "test",
      ImVec2(main_window_width_ - local_window_width_, main_window_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

  ImGui::SetWindowFontScale(0.5f);
  ImGui::Text(u8"远程控制桌面");
  ImGui::SetWindowFontScale(1.0f);

  ImGui::Spacing();

  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::BeginChild(u8"窗口2", ImVec2(330, 180), ImGuiChildFlags_Border);
  {
    ImGui::SetWindowFontScale(0.5f);

    ImGui::Text(localization::remote_id[localization_language_index_].c_str());

    ImGui::Spacing();
    ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::InputText(
        "##remote_id_", remote_id_, IM_ARRAYSIZE(remote_id_),
        ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_RIGHT_LONG, ImVec2(55, 35)) || rejoin_) {
      int ret = -1;
      if ("SignalConnected" == signal_status_str_) {
        if (connect_button_label_ ==
                localization::connect[localization_language_index_] &&
            !connection_established_ && strlen(remote_id_)) {
          if (remote_id_ == local_id_ && !peer_reserved_) {
            peer_reserved_ = CreatePeer(&params_);
            if (peer_reserved_) {
              LOG_INFO("Create peer[reserved] instance successful");
              std::string local_id = "C-" + mac_addr_str_;
              Init(peer_reserved_, local_id.c_str());
              LOG_INFO("Peer[reserved] init finish");
            } else {
              LOG_INFO("Create peer[reserved] instance failed");
            }
          }
          ret = JoinConnection(peer_reserved_ ? peer_reserved_ : peer_,
                               remote_id_, client_password_);
          if (0 == ret) {
            if (!peer_reserved_) {
              is_client_mode_ = true;
            }
            rejoin_ = false;
          } else {
            rejoin_ = true;
          }

        } else if (connect_button_label_ ==
                       localization::disconnect[localization_language_index_] &&
                   connection_established_) {
          ret = LeaveConnection(peer_reserved_ ? peer_reserved_ : peer_);

          if (0 == ret) {
            rejoin_ = false;
            memset(audio_buffer_, 0, 960);
            connection_established_ = false;
            received_frame_ = false;
            is_client_mode_ = false;
          }
        }

        if (0 == ret) {
          connect_button_pressed_ = !connect_button_pressed_;
          connect_button_label_ =
              connect_button_pressed_
                  ? localization::disconnect[localization_language_index_]
                  : localization::connect[localization_language_index_];
        }
      }
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::EndChild();

  return 0;
}