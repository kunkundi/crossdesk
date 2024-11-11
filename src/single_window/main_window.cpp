#include "IconsFontAwesome6.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::MainWindow() {
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  // ImGui::SetNextWindowSize(
  //     ImVec2(main_window_width_default_, main_window_height_default_),
  //     ImGuiCond_Always);

  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild("MainWindow",
                    ImVec2(main_window_width_default_, local_window_height_),
                    ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  LocalWindow();

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddLine(
      ImVec2(main_window_width_default_ / 2, title_bar_height_ + 15.0f),
      ImVec2(main_window_width_default_ / 2, title_bar_height_ + 225.0f),
      IM_COL32(0, 0, 0, 122), 1.0f);

  RemoteWindow();
  ImGui::EndChild();

  ImGui::SetNextWindowPos(
      ImVec2(0, title_bar_height_ + local_window_height_ - 1.0f),
      ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild(
      "RecentConnectionsWindow",
      ImVec2(main_window_width_default_,
             main_window_height_default_ - title_bar_height_ -
                 local_window_height_ - status_bar_height_ + 1.0f),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + main_window_text_y_padding_);
  ImGui::Indent(main_child_window_x_padding_);
  ImGui::SetWindowFontScale(0.8f);

  ImGui::TextColored(
      ImVec4(0.0f, 0.0f, 0.0f, 0.5f), "%s",
      localization::recent_connections[localization_language_index_].c_str());
  ImGui::SetWindowFontScale(1.0f);

  ShowRecentConnections();

  ImGui::EndChild();

  StatusBar();
  ConnectionStatusWindow();

  return 0;
}

int Render::ShowRecentConnections() {
  ImGui::SetCursorPosX(25.0f);
  ImVec2 sub_window_pos = ImGui::GetCursorPos();
  std::map<std::string, ImVec2> sub_containers_pos;
  int recent_connection_sub_container_width =
      recent_connection_image_width_ + 16.0f;
  int recent_connection_sub_container_height =
      recent_connection_image_height_ + 32.0f;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild("RecentConnectionsContainer",
                    ImVec2(main_window_width_default_ - 50.0f, 152.0f),
                    ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoBringToFrontOnFocus |
                        ImGuiWindowFlags_AlwaysHorizontalScrollbar);
  ImGui::PopStyleColor();
  int recent_connections_count = recent_connection_textures_.size();
  int count = 0;
  for (auto it = recent_connection_textures_.begin();
       it != recent_connection_textures_.end(); ++it) {
    sub_containers_pos[it->first] = ImGui::GetCursorPos();
    std::string recent_connection_sub_window_name =
        "RecentConnectionsSubContainer" + it->first;
    ImGui::BeginChild(recent_connection_sub_window_name.c_str(),
                      ImVec2(recent_connection_sub_container_width,
                             recent_connection_sub_container_height),
                      ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoBringToFrontOnFocus |
                          ImGuiWindowFlags_NoScrollbar);
    size_t pos1 = it->first.find('\\') + 1;
    size_t pos2 = it->first.rfind('@');
    std::string host_name = it->first.substr(pos1, pos2 - pos1);
    ImGui::SetWindowFontScale(0.4f);
    ImVec2 window_size = ImGui::GetWindowSize();
    ImVec2 text_size = ImGui::CalcTextSize(host_name.c_str());
    ImVec2 pos = ImGui::GetCursorPos();
    pos.x = (window_size.x - text_size.x) / 2.0f;
    ImGui::SetCursorPos(pos);
    ImGui::Text(host_name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Image((ImTextureID)(intptr_t)it->second,
                 ImVec2((float)recent_connection_image_width_,
                        (float)recent_connection_image_height_));

    ImGui::EndChild();

    count++;
    ImGui::SameLine(0, count != recent_connections_count ? 23.0f : 0.0f);
  }

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  for (const auto& pos : sub_containers_pos) {
    ImVec2 delete_button_pos =
        ImVec2(pos.second.x + recent_connection_sub_container_width - 11.0f,
               pos.second.y - 7.0f);
    ImGui::SetCursorPos(delete_button_pos);
    ImGui::SetWindowFontScale(0.5f);
    std::string xmake = ICON_FA_CIRCLE_XMARK;
    std::string recent_connection_delete_button_name =
        xmake + "##RecentConnectionDelete" +
        std::to_string(delete_button_pos.x);
    if (ImGui::SmallButton(recent_connection_delete_button_name.c_str())) {
      show_confirm_delete_connection_ = true;
    }

    if (delete_connection_) {
      if (!thumbnail_.DeleteThumbnail(pos.first)) {
        reload_recent_connections_ = true;
        delete_connection_ = false;
      }
    }
    ImGui::SetWindowFontScale(1.0f);
  }
  ImGui::PopStyleColor(3);

  ImGui::EndChild();

  if (show_confirm_delete_connection_) {
    ConfirmDeleteConnection();
  }

  return 0;
}

int Render::ConfirmDeleteConnection() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
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

  ImGui::Begin("ConfirmDeleteConnectionWindow", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  ImGui::SetWindowFontScale(0.5f);
  std::string text =
      localization::confirm_delete_connection[localization_language_index_];
  ImGui::SetCursorPosX(connection_status_window_width_ * 6 / 19);
  ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);

  // ok
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

  auto window_width = ImGui::GetWindowSize().x;
  auto window_height = ImGui::GetWindowSize().y;
  auto text_width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
  ImGui::SetCursorPosY(window_height * 0.2f);
  ImGui::Text(text.c_str());
  ImGui::SetWindowFontScale(1.0f);

  ImGui::End();
  ImGui::PopStyleVar();
  return 0;
}