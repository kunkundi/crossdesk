#include "localization.h"
#include "rd_log.h"
#include "render.h"

#define BUTTON_PADDING 36.0f

int Render::TitleBar(bool main_window) {
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetWindowFontScale(0.8f);
  ImGui::BeginChild(
      main_window ? "MainTitleBar" : "StreamTitleBar",
      ImVec2(main_window ? main_window_width_ : stream_window_width_,
             title_bar_height_),
      ImGuiChildFlags_Border,
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDecoration |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  if (ImGui::BeginMenuBar()) {
    ImGui::SetCursorPosX(
        (main_window ? main_window_width_ : stream_window_width_) -
        (BUTTON_PADDING * 3 - 3));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (main_window) {
      float bar_pos_x = ImGui::GetCursorPosX() + 6;
      float bar_pos_y = ImGui::GetCursorPosY() + 15;
      std::string menu_button = "    ";  // ICON_FA_BARS;
      if (ImGui::BeginMenu(menu_button.c_str())) {
        ImGui::SetWindowFontScale(0.5f);
        if (ImGui::MenuItem(
                localization::settings[localization_language_index_].c_str())) {
          show_settings_window_ = true;
        }
        if (ImGui::MenuItem(
                localization::about[localization_language_index_].c_str())) {
          show_about_window_ = true;
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndMenu();
      }
      float menu_bar_line_size = 15.0f;
      draw_list->AddLine(ImVec2(bar_pos_x, bar_pos_y - 6),
                         ImVec2(bar_pos_x + menu_bar_line_size, bar_pos_y - 6),
                         IM_COL32(0, 0, 0, 255));
      draw_list->AddLine(ImVec2(bar_pos_x, bar_pos_y),
                         ImVec2(bar_pos_x + menu_bar_line_size, bar_pos_y),
                         IM_COL32(0, 0, 0, 255));
      draw_list->AddLine(ImVec2(bar_pos_x, bar_pos_y + 6),
                         ImVec2(bar_pos_x + menu_bar_line_size, bar_pos_y + 6),
                         IM_COL32(0, 0, 0, 255));

      {
        SettingWindow();
        SelfHostedServerWindow();
        AboutWindow();
      }
    }

    ImGui::PopStyleColor(2);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::SetCursorPosX(main_window
                             ? (main_window_width_ - BUTTON_PADDING * 2)
                             : (stream_window_width_ - BUTTON_PADDING * 3));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    float minimize_pos_x = ImGui::GetCursorPosX() + 12;
    float minimize_pos_y = ImGui::GetCursorPosY() + 15;
    std::string window_minimize_button = "##minimize";  // ICON_FA_MINUS;
    if (ImGui::Button(window_minimize_button.c_str(),
                      ImVec2(BUTTON_PADDING, 30))) {
      SDL_MinimizeWindow(main_window ? main_window_ : stream_window_);
    }
    draw_list->AddLine(ImVec2(minimize_pos_x, minimize_pos_y),
                       ImVec2(minimize_pos_x + 12, minimize_pos_y),
                       IM_COL32(0, 0, 0, 255));
    ImGui::PopStyleColor(2);

    if (!main_window) {
      ImGui::SetCursorPosX(stream_window_width_ - BUTTON_PADDING * 2);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

      if (window_maximized_) {
        float pos_x_top = ImGui::GetCursorPosX() + 11;
        float pos_y_top = ImGui::GetCursorPosY() + 11;
        float pos_x_bottom = ImGui::GetCursorPosX() + 13;
        float pos_y_bottom = ImGui::GetCursorPosY() + 9;
        std::string window_restore_button =
            "##restore";  // ICON_FA_WINDOW_RESTORE;
        if (ImGui::Button(window_restore_button.c_str(),
                          ImVec2(BUTTON_PADDING, 30))) {
          SDL_RestoreWindow(stream_window_);
          window_maximized_ = false;
        }
        draw_list->AddRect(ImVec2(pos_x_top, pos_y_top),
                           ImVec2(pos_x_top + 12, pos_y_top + 12),
                           IM_COL32(0, 0, 0, 255));
        draw_list->AddRect(ImVec2(pos_x_bottom, pos_y_bottom),
                           ImVec2(pos_x_bottom + 12, pos_y_bottom + 12),
                           IM_COL32(0, 0, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos_x_top + 1, pos_y_top + 1),
                                 ImVec2(pos_x_top + 11, pos_y_top + 11),
                                 IM_COL32(255, 255, 255, 255));
      } else {
        float maximize_pos_x = ImGui::GetCursorPosX() + 12;
        float maximize_pos_y = ImGui::GetCursorPosY() + 10;
        std::string window_maximize_button =
            "##maximize";  // ICON_FA_SQUARE_FULL;
        if (ImGui::Button(window_maximize_button.c_str(),
                          ImVec2(BUTTON_PADDING, 30))) {
          SDL_MaximizeWindow(stream_window_);
          window_maximized_ = !window_maximized_;
        }
        draw_list->AddRect(ImVec2(maximize_pos_x, maximize_pos_y),
                           ImVec2(maximize_pos_x + 12, maximize_pos_y + 12),
                           IM_COL32(0, 0, 0, 255));
      }
      ImGui::PopStyleColor(2);
    }

    ImGui::SetCursorPosX(
        (main_window ? main_window_width_ : stream_window_width_) -
        BUTTON_PADDING);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0, 0, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0, 0, 0.5f));

    float xmark_pos_x = ImGui::GetCursorPosX() + 18;
    float xmark_pos_y = ImGui::GetCursorPosY() + 16;
    float xmark_size = 12.0f;
    std::string close_button = "##xmark";  // ICON_FA_XMARK;
    if (ImGui::Button(close_button.c_str(), ImVec2(BUTTON_PADDING, 30))) {
      SDL_Event event;
      event.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&event);
    }
    draw_list->AddLine(ImVec2(xmark_pos_x - xmark_size / 2 - 0.25f,
                              xmark_pos_y - xmark_size / 2 + 0.75f),
                       ImVec2(xmark_pos_x + xmark_size / 2 - 1.5f,
                              xmark_pos_y + xmark_size / 2 - 0.5f),
                       IM_COL32(0, 0, 0, 255));
    draw_list->AddLine(ImVec2(xmark_pos_x + xmark_size / 2 - 1.75f,
                              xmark_pos_y - xmark_size / 2 + 0.75f),
                       ImVec2(xmark_pos_x - xmark_size / 2,
                              xmark_pos_y + xmark_size / 2 - 1.0f),
                       IM_COL32(0, 0, 0, 255));

    ImGui::PopStyleColor(2);

    ImGui::PopStyleColor();
  }

  ImGui::EndMenuBar();
  ImGui::EndChild();
  ImGui::PopStyleColor();
  return 0;
}