#include <algorithm>
#include <string>
#include <vector>

#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

int Render::ServerWindow() {
  ImGui::SetNextWindowSize(ImVec2(server_window_width_, server_window_height_),
                           ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

  ImGui::Begin("##server_window", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

  server_window_title_bar_height_ = title_bar_height_;

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
  float remote_client_info_window_width = server_window_width_ * 0.8f;
  float remote_client_info_window_height =
      (server_window_height_ - server_window_title_bar_height_) * 0.9f;

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

  float font_scale = localization_language_index_ == 0 ? 0.5f : 0.45f;

  ImGui::SetWindowFontScale(localization_language_index_ == 0 ? 0.5f : 0.48f);

  std::vector<std::string> remote_ids;
  remote_ids.reserve(connection_status_.size());
  for (const auto& kv : connection_status_) {
    remote_ids.push_back(kv.first);
  }

  if (!selected_server_remote_id_.empty()) {
    if (std::find(remote_ids.begin(), remote_ids.end(),
                  selected_server_remote_id_) == remote_ids.end()) {
      selected_server_remote_id_.clear();
    }
  }
  if (selected_server_remote_id_.empty() && !remote_ids.empty()) {
    selected_server_remote_id_ = remote_ids.front();
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s",
              localization::controller[localization_language_index_].c_str());
  ImGui::SameLine();

  const char* selected_preview = "-";
  if (!selected_server_remote_id_.empty()) {
    selected_preview = selected_server_remote_id_.c_str();
  } else if (!remote_client_id_.empty()) {
    selected_preview = remote_client_id_.c_str();
  }

  ImGui::SetNextItemWidth(remote_client_info_window_width *
                          (localization_language_index_ == 0 ? 0.65f : 0.6f));
  if (ImGui::BeginCombo("##server_remote_id", selected_preview)) {
    ImGui::SetWindowFontScale(font_scale);
    for (int i = 0; i < static_cast<int>(remote_ids.size()); i++) {
      const bool selected = (remote_ids[i] == selected_server_remote_id_);
      if (ImGui::Selectable(remote_ids[i].c_str(), selected)) {
        selected_server_remote_id_ = remote_ids[i];
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::Separator();

  ImGui::SetWindowFontScale(font_scale);

  if (!selected_server_remote_id_.empty()) {
    auto it = connection_status_.find(selected_server_remote_id_);
    const ConnectionStatus status = (it == connection_status_.end())
                                        ? ConnectionStatus::Closed
                                        : it->second;

    ImGui::Text(
        "%s",
        localization::connection_status[localization_language_index_].c_str());
    ImGui::SameLine();

    switch (connection_status_[selected_server_remote_id_]) {
      case ConnectionStatus::Connected:
        ImGui::Text(
            "%s",
            localization::p2p_connected[localization_language_index_].c_str());
        break;
      case ConnectionStatus::Connecting:
        ImGui::Text(
            "%s",
            localization::p2p_connecting[localization_language_index_].c_str());
        break;
      case ConnectionStatus::Disconnected:
        ImGui::Text("%s",
                    localization::p2p_disconnected[localization_language_index_]
                        .c_str());
        break;
      case ConnectionStatus::Failed:
        ImGui::Text(
            "%s",
            localization::p2p_failed[localization_language_index_].c_str());
        break;
      case ConnectionStatus::Closed:
        ImGui::Text(
            "%s",
            localization::p2p_closed[localization_language_index_].c_str());
        break;
      default:
        ImGui::Text(
            "%s",
            localization::p2p_failed[localization_language_index_].c_str());
        break;
    }
  }

  ImGui::Separator();

  ImGui::AlignTextToFramePadding();
  ImGui::Text(
      "%s", localization::file_transfer[localization_language_index_].c_str());

  ImGui::SameLine();

  if (ImGui::Button(
          localization::select_file[localization_language_index_].c_str())) {
    std::string title = localization::select_file[localization_language_index_];
    std::string path = OpenFileDialog(title);
    LOG_INFO("Selected file path: {}", path.c_str());

    ProcessSelectedFile(path, nullptr, file_label_, selected_server_remote_id_);
  }

  ImGui::SetWindowFontScale(1.0f);

  ImGui::EndChild();

  ImGui::SameLine();

  float close_connection_button_width = server_window_width_ * 0.1f;
  float close_connection_button_height = remote_client_info_window_height;

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
  ImGui::SetWindowFontScale(font_scale);
  if (ImGui::Button(ICON_FA_XMARK, ImVec2(close_connection_button_width,
                                          close_connection_button_height))) {
    if (peer_ && !selected_server_remote_id_.empty()) {
      LeaveConnection(peer_, selected_server_remote_id_.c_str());
    }
  }
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();

  return 0;
}
}  // namespace crossdesk