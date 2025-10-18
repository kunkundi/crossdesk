#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::MainWindow() {
  ImGui::SetNextWindowPos(ImVec2(0, title_bar_height_), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild("DeskWindow",
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

  RecentConnectionsWindow();
  StatusBar();

  if (show_connection_status_window_) {
    for (auto it = client_properties_.begin();
         it != client_properties_.end();) {
      auto& props = it->second;
      if (focused_remote_id_ == props->remote_id_) {
        if (ConnectionStatusWindow(props)) {
          it = client_properties_.erase(it);
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
