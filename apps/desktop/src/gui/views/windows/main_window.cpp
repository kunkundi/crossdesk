#include "application/gui_application.h"
#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"

namespace crossdesk {

int GuiApplication::MainWindow() {
  ImGuiIO &io = ImGui::GetIO();
  float local_remote_window_width = io.DisplaySize.x;
  float local_remote_window_height =
      io.DisplaySize.y * (0.56f - TITLE_BAR_HEIGHT);

  ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y * (TITLE_BAR_HEIGHT)),
                          ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  ImGui::BeginChild(
      "DeskWindow",
      ImVec2(local_remote_window_width, local_remote_window_height),
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);

  LocalWindow();

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddLine(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.1f),
                     ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.53f),
                     IM_COL32(0, 0, 0, 122), 1.0f);

  RemoteWindow();
  ImGui::EndChild();

  RecentConnectionsWindow();
  StatusBar();

  if (show_connection_status_window_) {
    // std::unique_lock lock(remote_sessions_mutex_);
    for (auto it = remote_sessions_.begin();
         it != remote_sessions_.end();) {
      auto &props = it->second;
      if (focused_remote_id_ == props->remote_id_) {
        if (ConnectionStatusWindow(props)) {
          it = remote_sessions_.erase(it);
        } else {
          ++it;
        }
      } else {
        ++it;
      }
    }
  }

  return 0;
}
} // namespace crossdesk
