#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

int Render::RecentConnectionsWindow() {
  ImGuiIO& io = ImGui::GetIO();
  float recent_connection_window_width = io.DisplaySize.x;
  float recent_connection_window_height =
      io.DisplaySize.y * (0.46f - STATUS_BAR_HEIGHT);
  ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y * 0.55f),
                          ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild(
      "RecentConnectionsWindow",
      ImVec2(recent_connection_window_width, recent_connection_window_height),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SetCursorPos(
      ImVec2(io.DisplaySize.x * 0.045f, io.DisplaySize.y * 0.02f));

  ImGui::SetWindowFontScale(0.9f);
  ImGui::TextColored(
      ImVec4(0.0f, 0.0f, 0.0f, 0.5f), "%s",
      localization::recent_connections[localization_language_index_].c_str());

  ShowRecentConnections();

  ImGui::EndChild();

  return 0;
}

int Render::ShowRecentConnections() {
  ImGuiIO& io = ImGui::GetIO();
  float recent_connection_panel_width = io.DisplaySize.x * 0.912f;
  float recent_connection_panel_height = io.DisplaySize.y * 0.29f;
  float recent_connection_image_height = recent_connection_panel_height * 0.6f;
  float recent_connection_image_width = recent_connection_image_height * 16 / 9;
  float recent_connection_sub_container_width =
      recent_connection_image_width * 1.2f;
  float recent_connection_sub_container_height =
      recent_connection_image_height * 1.4f;
  float recent_connection_button_width = recent_connection_image_width * 0.15f;
  float recent_connection_button_height =
      recent_connection_image_height * 0.25f;
  float recent_connection_dummy_button_width =
      recent_connection_image_width - 2 * recent_connection_button_width;

  ImGui::SetCursorPos(
      ImVec2(io.DisplaySize.x * 0.045f, io.DisplaySize.y * 0.1f));

  std::map<std::string, ImVec2> sub_containers_pos;
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImVec4(239.0f / 255, 240.0f / 255, 242.0f / 255, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::BeginChild(
      "RecentConnectionsContainer",
      ImVec2(recent_connection_panel_width, recent_connection_panel_height),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus |
          ImGuiWindowFlags_AlwaysHorizontalScrollbar |
          ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  size_t recent_connections_count = recent_connections_.size();
  int count = 0;
  for (auto& it : recent_connections_) {
    sub_containers_pos[it.first] = ImGui::GetCursorPos();
    std::string recent_connection_sub_window_name =
        "RecentConnectionsSubContainer" + it.first;
    // recent connections sub container
    ImGui::BeginChild(recent_connection_sub_window_name.c_str(),
                      ImVec2(recent_connection_sub_container_width,
                             recent_connection_sub_container_height),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoBringToFrontOnFocus);
    std::string connection_info = it.first;

    // remote id length is 9
    // password length is 6
    // connection_info -> remote_id + 'Y' + host_name + '@' + password
    //                 -> remote_id + 'N' + host_name
    if ('Y' == connection_info[9] && connection_info.size() >= 16) {
      size_t pos_y = connection_info.find('Y');
      size_t pos_at = connection_info.find('@');

      if (pos_y == std::string::npos || pos_at == std::string::npos ||
          pos_y >= pos_at) {
        LOG_ERROR("Invalid filename");
        continue;
      }

      it.second.remote_id = connection_info.substr(0, pos_y);
      it.second.remote_host_name =
          connection_info.substr(pos_y + 1, pos_at - pos_y - 1);
      it.second.password = connection_info.substr(pos_at + 1);
      it.second.remember_password = true;
    } else if ('N' == connection_info[9] && connection_info.size() >= 10) {
      size_t pos_n = connection_info.find('N');
      size_t pos_at = connection_info.find('@');

      if (pos_n == std::string::npos) {
        LOG_ERROR("Invalid filename");
        continue;
      }

      it.second.remote_id = connection_info.substr(0, pos_n);
      it.second.remote_host_name = connection_info.substr(pos_n + 1);
      it.second.password = "";
      it.second.remember_password = false;
    } else {
      it.second.remote_host_name = "unknown";
    }

    ImVec2 image_screen_pos = ImVec2(
        ImGui::GetCursorScreenPos().x + recent_connection_image_width * 0.04f,
        ImGui::GetCursorScreenPos().y + recent_connection_image_height * 0.08f);
    ImVec2 image_pos =
        ImVec2(ImGui::GetCursorPosX() + recent_connection_image_width * 0.05f,
               ImGui::GetCursorPosY() + recent_connection_image_height * 0.08f);
    ImGui::SetCursorPos(image_pos);
    ImGui::Image(
        (ImTextureID)(intptr_t)it.second.texture,
        ImVec2(recent_connection_image_width, recent_connection_image_height));

    // remote id display button
    {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.2f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0.2f));

      ImVec2 dummy_button_pos =
          ImVec2(image_pos.x, image_pos.y + recent_connection_image_height);
      std::string dummy_button_name = "##DummyButton" + it.second.remote_id;
      ImGui::SetCursorPos(dummy_button_pos);
      ImGui::SetWindowFontScale(0.6f);
      ImGui::Button(dummy_button_name.c_str(),
                    ImVec2(recent_connection_dummy_button_width,
                           recent_connection_button_height));
      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetCursorPos(ImVec2(
          dummy_button_pos.x + recent_connection_dummy_button_width * 0.05f,
          dummy_button_pos.y + recent_connection_button_height * 0.05f));
      ImGui::SetWindowFontScale(0.65f);
      ImGui::Text("%s", it.second.remote_id.c_str());
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleColor(3);

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::SetWindowFontScale(0.5f);
        ImGui::Text("%s", it.second.remote_host_name.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndTooltip();
      }
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.1f, 0.4f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 0.7f));
    ImGui::SetWindowFontScale(0.5f);
    // trash button
    {
      ImVec2 trash_can_button_pos =
          ImVec2(image_pos.x + recent_connection_image_width -
                     2 * recent_connection_button_width,
                 image_pos.y + recent_connection_image_height);
      ImGui::SetCursorPos(trash_can_button_pos);
      std::string trash_can = ICON_FA_TRASH_CAN;
      std::string recent_connection_delete_button_name =
          trash_can + "##RecentConnectionDelete" +
          std::to_string(trash_can_button_pos.x);
      if (ImGui::Button(recent_connection_delete_button_name.c_str(),
                        ImVec2(recent_connection_button_width,
                               recent_connection_button_height))) {
        show_confirm_delete_connection_ = true;
        delete_connection_name_ = it.first;
      }

      if (delete_connection_ && delete_connection_name_ == it.first) {
        if (!thumbnail_->DeleteThumbnail(it.first)) {
          reload_recent_connections_ = true;
          delete_connection_ = false;
        }
      }
    }

    // connect button
    {
      ImVec2 connect_button_pos =
          ImVec2(image_pos.x + recent_connection_image_width -
                     recent_connection_button_width,
                 image_pos.y + recent_connection_image_height);
      ImGui::SetCursorPos(connect_button_pos);
      std::string connect = ICON_FA_ARROW_RIGHT_LONG;
      std::string connect_to_this_connection_button_name =
          connect + "##ConnectionTo" + it.first;
      if (ImGui::Button(connect_to_this_connection_button_name.c_str(),
                        ImVec2(recent_connection_button_width,
                               recent_connection_button_height))) {
        ConnectTo(it.second.remote_id, it.second.password.c_str(),
                  it.second.remember_password);
      }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor(3);

    ImGui::EndChild();

    if (count != recent_connections_count - 1) {
      ImVec2 line_start =
          ImVec2(image_screen_pos.x + recent_connection_image_width * 1.19f,
                 image_screen_pos.y);
      ImVec2 line_end =
          ImVec2(image_screen_pos.x + recent_connection_image_width * 1.19f,
                 image_screen_pos.y + recent_connection_image_height +
                     recent_connection_button_height);
      ImGui::GetWindowDrawList()->AddLine(line_start, line_end,
                                          IM_COL32(0, 0, 0, 122), 1.0f);
    }

    count++;
    ImGui::SameLine(0, count != recent_connections_count
                           ? (recent_connection_image_width * 0.165f)
                           : 0.0f);
  }

  ImGui::EndChild();

  if (show_confirm_delete_connection_) {
    ConfirmDeleteConnection();
  }

  return 0;
}

int Render::ConfirmDeleteConnection() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));
  ImGui::SetNextWindowSize(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0, 1.0, 1.0, 1.0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);

  ImGui::Begin("ConfirmDeleteConnectionWindow", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  auto connection_status_window_width = ImGui::GetWindowSize().x;
  auto connection_status_window_height = ImGui::GetWindowSize().y;

  std::string text =
      localization::confirm_delete_connection[localization_language_index_];
  ImGui::SetCursorPosX(connection_status_window_width * 0.33f);
  ImGui::SetCursorPosY(connection_status_window_height * 0.67f);

  // ok
  ImGui::SetWindowFontScale(0.5f);
  if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
      ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    delete_connection_ = true;
    show_confirm_delete_connection_ = false;
  }

  ImGui::SameLine();
  // cancel
  if (ImGui::Button(
          localization::cancel[localization_language_index_].c_str()) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    delete_connection_ = false;
    show_confirm_delete_connection_ = false;
  }

  auto text_width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SetCursorPosX((connection_status_window_width - text_width) * 0.5f);
  ImGui::SetCursorPosY(connection_status_window_height * 0.2f);
  ImGui::Text("%s", text.c_str());
  ImGui::SetWindowFontScale(1.0f);

  ImGui::End();
  ImGui::PopStyleVar();
  return 0;
}
}  // namespace crossdesk