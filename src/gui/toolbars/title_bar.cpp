#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

#define NEW_VERSION_ICON_RENDER_TIME_INTERVAL 2000

namespace crossdesk {

int Render::TitleBar(bool main_window) {
  ImGuiIO& io = ImGui::GetIO();
  float title_bar_width = io.DisplaySize.x;
  float title_bar_height = io.DisplaySize.y * (TITLE_BAR_HEIGHT + 0.01f);
  float title_bar_button_width = io.DisplaySize.x * TITLE_BAR_BUTTON_WIDTH;
  float title_bar_button_height = io.DisplaySize.y * TITLE_BAR_BUTTON_HEIGHT;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::BeginChild(main_window ? "MainTitleBar" : "StreamTitleBar",
                    ImVec2(title_bar_width, title_bar_height),
                    ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  // get draw list
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  ImGui::SetCursorPos(
      ImVec2(title_bar_width - title_bar_button_width * 3, 0.0f));
  if (main_window) {
    float bar_pos_x = title_bar_width - title_bar_button_width * 3 +
                      title_bar_button_width * 0.33f;
    float bar_pos_y = title_bar_button_height * 0.5f;

    // draw menu icon
    float menu_bar_line_size = title_bar_button_width * 0.33f;
    draw_list->AddLine(
        ImVec2(bar_pos_x, bar_pos_y - title_bar_button_height * 0.15f),
        ImVec2(bar_pos_x + menu_bar_line_size,
               bar_pos_y - title_bar_button_height * 0.15f),
        IM_COL32(0, 0, 0, 255));
    draw_list->AddLine(ImVec2(bar_pos_x, bar_pos_y),
                       ImVec2(bar_pos_x + menu_bar_line_size, bar_pos_y),
                       IM_COL32(0, 0, 0, 255));
    draw_list->AddLine(
        ImVec2(bar_pos_x, bar_pos_y + title_bar_button_height * 0.15f),
        ImVec2(bar_pos_x + menu_bar_line_size,
               bar_pos_y + title_bar_button_height * 0.15f),
        IM_COL32(0, 0, 0, 255));

    std::string title_bar_menu_button = "##title_bar_menu";  // ICON_FA_BARS;
    std::string title_bar_menu = "##title_bar_menu";

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button(
            title_bar_menu_button.c_str(),
            ImVec2(title_bar_button_width, title_bar_button_height))) {
      ImGui::OpenPopup(title_bar_menu.c_str());
    }
    ImGui::PopStyleColor(3);

    if (ImGui::BeginPopup(title_bar_menu.c_str())) {
      ImGui::SetWindowFontScale(0.6f);
      if (ImGui::MenuItem(
              localization::settings[localization_language_index_].c_str())) {
        show_settings_window_ = true;
      }

      show_new_version_icon_in_menu_ = false;

      std::string about_str = localization::about[localization_language_index_];
      if (update_available_) {
        auto now_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();

        // every 2 seconds
        if (now_time - new_version_icon_last_trigger_time_ >=
            NEW_VERSION_ICON_RENDER_TIME_INTERVAL) {
          show_new_version_icon_ = true;
          new_version_icon_render_start_time_ = now_time;
          new_version_icon_last_trigger_time_ = now_time;
        }

        // render for 1 second
        if (show_new_version_icon_) {
          about_str = about_str + " " + ICON_FA_TRIANGLE_EXCLAMATION;
          if (now_time - new_version_icon_render_start_time_ >=
              NEW_VERSION_ICON_RENDER_TIME_INTERVAL / 2) {
            show_new_version_icon_ = false;
          }
        } else {
          about_str = about_str + "      ";
        }
      }

      if (ImGui::MenuItem(about_str.c_str())) {
        show_about_window_ = true;
      }

      if (update_available_ && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::SetWindowFontScale(0.5f * dpi_scale_);
        std::string new_version_available_str =
            localization::new_version_available[localization_language_index_] +
            ": " + latest_version_;
        ImGui::Text("%s", new_version_available_str.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndTooltip();
      }

      ImGui::EndMenu();
    } else {
      show_new_version_icon_in_menu_ = true;
    }

    if (update_available_ && show_new_version_icon_in_menu_) {
      auto now_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();

      // every 2 seconds
      if (now_time - new_version_icon_last_trigger_time_ >=
          NEW_VERSION_ICON_RENDER_TIME_INTERVAL) {
        show_new_version_icon_ = true;
        new_version_icon_render_start_time_ = now_time;
        new_version_icon_last_trigger_time_ = now_time;
      }

      // render for 1 second
      if (show_new_version_icon_) {
        ImGui::SetWindowFontScale(0.6f);
        ImGui::SetCursorPos(ImVec2(bar_pos_x + 10, bar_pos_y - 17));
        ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::SetWindowFontScale(1.0f);

        if (now_time - new_version_icon_render_start_time_ >=
            NEW_VERSION_ICON_RENDER_TIME_INTERVAL / 2) {
          show_new_version_icon_ = false;
        }
      }
    }

    {
      SettingWindow();
      SelfHostedServerWindow();
      AboutWindow();
    }
  } else {
    ImGui::SetWindowFontScale(1.2f);
  }

  ImGui::SetCursorPos(ImVec2(
      title_bar_width - title_bar_button_width * (main_window ? 2 : 3), 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

  float minimize_pos_x = title_bar_width -
                         title_bar_button_width * (main_window ? 2 : 3) +
                         title_bar_button_width * 0.33f;
  float minimize_pos_y = title_bar_button_height * 0.5f;
  std::string window_minimize_button = "##minimize";  // ICON_FA_MINUS;
  if (ImGui::Button(window_minimize_button.c_str(),
                    ImVec2(title_bar_button_width, title_bar_button_height))) {
    SDL_MinimizeWindow(main_window ? main_window_ : stream_window_);
  }
  draw_list->AddLine(
      ImVec2(minimize_pos_x, minimize_pos_y),
      ImVec2(minimize_pos_x + title_bar_button_width * 0.33f, minimize_pos_y),
      IM_COL32(0, 0, 0, 255));
  ImGui::PopStyleColor(3);

  if (!main_window) {
    ImGui::SetCursorPos(
        ImVec2(title_bar_width - title_bar_button_width * 2, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (!window_maximized_) {
      float pos_x_top = title_bar_width - title_bar_button_width * 1.65f;
      float pos_y_top = title_bar_button_height * 0.36f;
      float pos_x_bottom = title_bar_width - title_bar_button_width * 1.6f;
      float pos_y_bottom = title_bar_button_height * 0.28f;
      std::string window_restore_button =
          "##restore";  // ICON_FA_WINDOW_RESTORE;
      if (ImGui::Button(
              window_restore_button.c_str(),
              ImVec2(title_bar_button_width, title_bar_button_height))) {
        SDL_RestoreWindow(stream_window_);
        window_maximized_ = false;
      }
      draw_list->AddRect(ImVec2(pos_x_top, pos_y_top),
                         ImVec2(pos_x_top + title_bar_button_height * 0.33f,
                                pos_y_top + title_bar_button_height * 0.33f),
                         IM_COL32(0, 0, 0, 255));
      draw_list->AddRect(ImVec2(pos_x_bottom, pos_y_bottom),
                         ImVec2(pos_x_bottom + title_bar_button_height * 0.33f,
                                pos_y_bottom + title_bar_button_height * 0.33f),
                         IM_COL32(0, 0, 0, 255));
      if (ImGui::IsItemHovered()) {
        draw_list->AddRectFilled(
            ImVec2(pos_x_top + title_bar_button_height * 0.02f,
                   pos_y_top + title_bar_button_height * 0.01f),
            ImVec2(pos_x_top + title_bar_button_height * 0.32f,
                   pos_y_top + title_bar_button_height * 0.31f),
            IM_COL32(229, 229, 229, 255));
      } else {
        draw_list->AddRectFilled(
            ImVec2(pos_x_top + title_bar_button_height * 0.02f,
                   pos_y_top + title_bar_button_height * 0.01f),
            ImVec2(pos_x_top + title_bar_button_height * 0.32f,
                   pos_y_top + title_bar_button_height * 0.31f),
            IM_COL32(255, 255, 255, 255));
      }
    } else {
      float maximize_pos_x = title_bar_width - title_bar_button_width * 1.5f -
                             title_bar_button_height * 0.165f;
      float maximize_pos_y = title_bar_button_height * 0.33f;
      std::string window_maximize_button =
          "##maximize";  // ICON_FA_SQUARE_FULL;
      if (ImGui::Button(
              window_maximize_button.c_str(),
              ImVec2(title_bar_button_width, title_bar_button_height))) {
        SDL_MaximizeWindow(stream_window_);
        window_maximized_ = !window_maximized_;
      }
      draw_list->AddRect(
          ImVec2(maximize_pos_x, maximize_pos_y),
          ImVec2(maximize_pos_x + title_bar_button_height * 0.33f,
                 maximize_pos_y + title_bar_button_height * 0.33f),
          IM_COL32(0, 0, 0, 255));
    }
    ImGui::PopStyleColor(3);
  }

  float xmark_button_pos_x = title_bar_width - title_bar_button_width;
  ImGui::SetCursorPos(ImVec2(xmark_button_pos_x, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0, 0, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0, 0, 0.5f));

  float xmark_pos_x = xmark_button_pos_x + title_bar_button_width * 0.5f;
  float xmark_pos_y = title_bar_button_height * 0.5f;
  float xmark_size = title_bar_button_width * 0.33f;
  std::string close_button = "##xmark";  // ICON_FA_XMARK;
  if (ImGui::Button(close_button.c_str(),
                    ImVec2(title_bar_button_width, title_bar_button_height))) {
#if _WIN32
    if (enable_minimize_to_tray_) {
      tray_->MinimizeToTray();
    } else {
#endif
      SDL_Event event;
      event.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&event);
#if _WIN32
    }
#endif
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
  return 0;
}
}  // namespace crossdesk