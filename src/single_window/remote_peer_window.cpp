#include "IconsFontAwesome6.h"
#include "layout_style.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::RemoteWindow() {
  ImGui::SetNextWindowPos(ImVec2(local_window_width_ - 1, menu_window_height_),
                          ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild(
      "RemoteDesktopWindow",
      ImVec2(main_window_width_ - local_window_width_ + 1,
             main_window_height_ - menu_window_height_ - status_bar_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  ImGui::SetWindowFontScale(1.0f);
  ImGui::Text(
      "%s", localization::remote_desktop[localization_language_index_].c_str());

  ImGui::Spacing();

  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImVec4(239.0 / 255, 240.0 / 255, 242.0 / 255, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);

  ImGui::BeginChild("RemoteDesktopWindow_1", ImVec2(330, 180),
                    ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  {
    ImGui::SetWindowFontScale(0.5f);
    ImGui::Text("%s",
                localization::remote_id[localization_language_index_].c_str());

    ImGui::Spacing();
    ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::InputText(
        "##remote_id_", remote_id_, IM_ARRAYSIZE(remote_id_),
        ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    ImGui::PopStyleVar();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_RIGHT_LONG, ImVec2(55, 38)) || rejoin_) {
      connect_button_pressed_ = true;
      connection_status_ = ConnectionStatus::Connecting;
      int ret = -1;
      if (signal_connected_) {
        if (!connection_established_) {
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
                               remote_id_, remote_password_.c_str());
          if (0 == ret) {
            if (peer_reserved_) {
              is_client_mode_ = true;
            }
            rejoin_ = false;
          } else {
            rejoin_ = true;
          }
        }
      }
    }
  }
  ImGui::EndChild();
  ImGui::EndChild();
  ImGui::PopStyleVar();

  return 0;
}