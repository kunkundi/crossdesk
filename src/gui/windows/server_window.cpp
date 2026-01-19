#include "rd_log.h"
#include "render.h"

namespace crossdesk {

namespace {
constexpr float kDragThresholdPx = 3.0f;

// Handles dragging for the *last submitted ImGui item*.
// `reset_on_deactivate` should be false when the caller needs to know whether a
// deactivation was a click (no drag) vs a drag-release (dragging==true).
inline void HandleWindowDragForLastItem(SDL_Window* window, bool& dragging,
                                        float& start_mouse_x,
                                        float& start_mouse_y, int& start_win_x,
                                        int& start_win_y,
                                        bool reset_on_deactivate = true) {
  if (!window) {
    return;
  }

  if (ImGui::IsItemActivated()) {
    SDL_GetGlobalMouseState(&start_mouse_x, &start_mouse_y);
    SDL_GetWindowPosition(window, &start_win_x, &start_win_y);
    dragging = false;
  }

  if (ImGui::IsItemActive()) {
    if (!dragging &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragThresholdPx)) {
      dragging = true;
    }

    if (dragging) {
      float mouse_x = 0.0f;
      float mouse_y = 0.0f;
      SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
      const int dx = (int)(mouse_x - start_mouse_x);
      const int dy = (int)(mouse_y - start_mouse_y);
      SDL_SetWindowPosition(window, start_win_x + dx, start_win_y + dy);
    }
  }

  if (reset_on_deactivate && ImGui::IsItemDeactivated()) {
    dragging = false;
  }
}
}  // namespace

int Render::ServerWindow() {
  ImGui::SetNextWindowSize(ImVec2(server_window_width_, server_window_height_),
                           ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

  ImGui::Begin("##server_window", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

  // Collapsed mode: no buttons; drag to move; click to restore.
  if (server_window_collapsed_) {
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("##server_collapsed_area",
                           ImVec2(server_window_width_, server_window_height_));

    HandleWindowDragForLastItem(server_window_,
                                server_window_collapsed_dragging_,
                                server_window_collapsed_drag_start_mouse_x_,
                                server_window_collapsed_drag_start_mouse_y_,
                                server_window_collapsed_drag_start_win_x_,
                                server_window_collapsed_drag_start_win_y_,
                                /*reset_on_deactivate=*/false);

    const bool request_restore =
        ImGui::IsItemDeactivated() && !server_window_collapsed_dragging_;
    if (ImGui::IsItemDeactivated()) {
      server_window_collapsed_dragging_ = false;
    }

    if (request_restore && server_window_) {
      int w = 0;
      int x = 0;
      int y = 0;
      int h = 0;
      SDL_GetWindowSize(server_window_, &w, &h);
      SDL_GetWindowPosition(server_window_, &x, &y);

      const int normal_h = server_window_normal_height_;
      SDL_SetWindowSize(server_window_, w, normal_h);
      SDL_SetWindowPosition(server_window_, x, y);
      server_window_collapsed_ = false;
    }

    ImGui::End();
    return 0;
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::BeginChild(
      "ServerTitleBar",
      ImVec2(server_window_width_, server_window_title_bar_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  float server_title_bar_button_width = server_window_title_bar_height_;
  float server_title_bar_button_height = server_window_title_bar_height_;

  // Drag area: the title bar excluding the right-side buttons.
  {
    const float drag_w =
        server_window_width_ - server_title_bar_button_width * 2;
    const float drag_h = server_title_bar_button_height;
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("##server_title_drag_area", ImVec2(drag_w, drag_h));

    HandleWindowDragForLastItem(
        server_window_, server_window_dragging_,
        server_window_drag_start_mouse_x_, server_window_drag_start_mouse_y_,
        server_window_drag_start_win_x_, server_window_drag_start_win_y_);
  }

  float minimize_button_pos_x =
      server_window_width_ - server_title_bar_button_width * 2;
  ImGui::SetCursorPos(ImVec2(minimize_button_pos_x, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

  float minimize_pos_x =
      minimize_button_pos_x + server_title_bar_button_width * 0.33f;
  float minimize_pos_y = server_title_bar_button_height * 0.5f;
  std::string server_minimize_button = "##minimize";  // ICON_FA_MINUS;
  if (ImGui::Button(server_minimize_button.c_str(),
                    ImVec2(server_title_bar_button_width,
                           server_title_bar_button_height))) {
    if (server_window_) {
      int w = 0;
      int h = 0;
      int x = 0;
      int y = 0;
      SDL_GetWindowSize(server_window_, &w, &h);
      SDL_GetWindowPosition(server_window_, &x, &y);

      const int collapsed_h = (int)server_window_title_bar_height_;
      // Collapse upward: keep top edge stable.
      SDL_SetWindowSize(server_window_, w, collapsed_h);
      SDL_SetWindowPosition(server_window_, x, y);
      server_window_collapsed_ = true;
    }
  }
  draw_list->AddLine(
      ImVec2(minimize_pos_x, minimize_pos_y),
      ImVec2(minimize_pos_x + server_title_bar_button_width * 0.33f,
             minimize_pos_y),
      IM_COL32(0, 0, 0, 255));
  ImGui::PopStyleColor(3);

  float xmark_button_pos_x =
      server_window_width_ - server_title_bar_button_width;
  ImGui::SetCursorPos(ImVec2(xmark_button_pos_x, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0, 0, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0, 0, 0.5f));

  float xmark_pos_x = xmark_button_pos_x + server_title_bar_button_width * 0.5f;
  float xmark_pos_y = server_title_bar_button_height * 0.5f;
  float xmark_size = server_title_bar_button_width * 0.33f;
  std::string server_close_button = "##xmark";  // ICON_FA_XMARK;
  if (ImGui::Button(server_close_button.c_str(),
                    ImVec2(server_title_bar_button_width,
                           server_title_bar_button_height))) {
    LOG_ERROR("Close button clicked");
    LeaveConnection(peer_, self_hosted_id_);
  }

  draw_list->AddLine(ImVec2(xmark_pos_x - xmark_size / 2 - 0.25f,
                            xmark_pos_y - xmark_size / 2 + 0.75f),
                     ImVec2(xmark_pos_x + xmark_size / 2 - 1.5f,
                            xmark_pos_y + xmark_size / 2 - 0.5f),
                     IM_COL32(0, 0, 0, 255));
  draw_list->AddLine(
      ImVec2(xmark_pos_x + xmark_size / 2 - 1.75f,
             xmark_pos_y - xmark_size / 2 + 0.75f),
      ImVec2(xmark_pos_x - xmark_size / 2, xmark_pos_y + xmark_size / 2 - 1.0f),
      IM_COL32(0, 0, 0, 255));

  ImGui::PopStyleColor(3);

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::End();
  return 0;
}
}  // namespace crossdesk