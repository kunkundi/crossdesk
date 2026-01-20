#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

namespace {}  // namespace

int Render::ServerWindow() {
  ImGui::SetNextWindowSize(ImVec2(server_window_width_, server_window_height_),
                           ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

  ImGui::Begin("##server_window", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::BeginChild(
      "ServerTitleBar",
      ImVec2(server_window_width_, server_window_title_bar_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);

  float server_title_bar_button_width = server_window_title_bar_height_;
  float server_title_bar_button_height = server_window_title_bar_height_;

  // Collapse/expand toggle button (FontAwesome icon).
  {
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    ImGui::SetWindowFontScale(0.5f);
    const char* icon =
        server_window_collapsed_ ? ICON_FA_ANGLE_DOWN : ICON_FA_ANGLE_UP;
    std::string toggle_label = std::string(icon) + "##server_toggle";
    if (ImGui::Button(toggle_label.c_str(),
                      ImVec2(server_title_bar_button_width,
                             server_title_bar_button_height))) {
      if (server_window_) {
        int w = 0;
        int h = 0;
        int x = 0;
        int y = 0;
        SDL_GetWindowSize(server_window_, &w, &h);
        SDL_GetWindowPosition(server_window_, &x, &y);

        if (server_window_collapsed_) {
          const int normal_h = server_window_normal_height_;
          SDL_SetWindowSize(server_window_, w, normal_h);
          SDL_SetWindowPosition(server_window_, x, y);
          server_window_collapsed_ = false;
        } else {
          const int collapsed_h = (int)server_window_title_bar_height_;
          // Collapse upward: keep top edge stable.
          SDL_SetWindowSize(server_window_, w, collapsed_h);
          SDL_SetWindowPosition(server_window_, x, y);
          server_window_collapsed_ = true;
        }
      }
    }
    ImGui::SetWindowFontScale(1.0f);

    ImGui::PopStyleColor(3);
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  RemoteClientInfoWindow();

  ImGui::End();
  return 0;
}

int Render::RemoteClientInfoWindow() {
  float remote_client_info_window_width = server_window_width_ * 0.75f;
  float remote_client_info_window_height =
      (server_window_height_ - server_window_title_bar_height_) * 0.3f;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
  ImGui::BeginChild(
      "RemoteClientInfoWindow",
      ImVec2(remote_client_info_window_width, remote_client_info_window_height),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SetWindowFontScale(0.7f);
  ImGui::Text("%s",
              localization::controller[localization_language_index_].c_str());
  ImGui::SameLine();
  ImGui::Text("%s", remote_client_id_.c_str());

  ImGui::SetWindowFontScale(1.0f);

  ImGui::EndChild();

  ImGui::SameLine();

  float close_connection_button_width = server_window_width_ * 0.15f;
  float close_connection_button_height = remote_client_info_window_height;

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
  ImGui::SetWindowFontScale(0.7f);
  if (ImGui::Button(ICON_FA_XMARK, ImVec2(close_connection_button_width,
                                          close_connection_button_height))) {
    LeaveConnection(peer_, self_hosted_id_);
  }
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();

  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  ImGui::BeginChild(
      "RemoteClientInfoFileTransferWindow",
      ImVec2(remote_client_info_window_width, remote_client_info_window_height),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  ImGui::SetWindowFontScale(0.7f);
  ImGui::AlignTextToFramePadding();
  ImGui::Text(
      "%s", localization::file_transfer[localization_language_index_].c_str());

  ImGui::SameLine();

  if (ImGui::Button(
          localization::select_file[localization_language_index_].c_str())) {
    std::string title = localization::select_file[localization_language_index_];
    std::string path = OpenFileDialog(title);
    LOG_INFO("Selected file path: {}", path.c_str());
  }
  ImGui::SetWindowFontScale(1.0f);

  ImGui::EndChild();

  return 0;
}
}  // namespace crossdesk