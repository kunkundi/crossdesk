#include "IconsFontAwesome6.h"
#include "localization.h"
#include "render.h"

#define BUTTON_PADDING 36.0f

int Render::TitleBar() {
  // ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1);

  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(1, 1, 1, 0.0f));
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetWindowFontScale(0.8f);
  ImGui::BeginChild("TitleBar", ImVec2(main_window_width_, title_bar_height_),
                    ImGuiChildFlags_Border,
                    ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDecoration |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);

  if (ImGui::BeginMenuBar()) {
    ImGui::SetCursorPosX(main_window_width_ - (streaming_
                                                   ? BUTTON_PADDING * 4 - 3
                                                   : BUTTON_PADDING * 3 - 3));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::BeginMenu(ICON_FA_BARS)) {
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
    ImGui::PopStyleColor(2);

    {
      SettingWindow();
      AboutWindow();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::SetCursorPosX(main_window_width_ - (streaming_
                                                   ? BUTTON_PADDING * 3
                                                   : BUTTON_PADDING * 2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    std::string window_minimize_button = ICON_FA_MINUS;
    if (ImGui::Button(window_minimize_button.c_str(),
                      ImVec2(BUTTON_PADDING, 30))) {
      SDL_MinimizeWindow(main_window_);
    }
    ImGui::PopStyleColor(2);

    if (streaming_) {
      ImGui::SetCursorPosX(main_window_width_ - BUTTON_PADDING * 2);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

      if (window_maximized_) {
        std::string window_restore_button = ICON_FA_WINDOW_RESTORE;
        if (ImGui::Button(window_restore_button.c_str(),
                          ImVec2(BUTTON_PADDING, 30))) {
          SDL_RestoreWindow(main_window_);
          window_maximized_ = !window_maximized_;
        }
      } else {
        std::string window_maximize_button = ICON_FA_SQUARE_FULL;
        if (ImGui::Button(window_maximize_button.c_str(),
                          ImVec2(BUTTON_PADDING, 30))) {
          SDL_MaximizeWindow(main_window_);
          window_maximized_ = !window_maximized_;
        }
      }
      ImGui::PopStyleColor(2);
    }

    ImGui::SetCursorPosX(main_window_width_ - BUTTON_PADDING);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0, 0, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0, 0, 0.5f));
    std::string close_button = ICON_FA_XMARK;
    if (ImGui::Button(close_button.c_str(), ImVec2(BUTTON_PADDING, 30))) {
      SDL_Event event;
      event.type = SDL_QUIT;
      SDL_PushEvent(&event);
    }
    ImGui::PopStyleColor(2);

    ImGui::PopStyleColor(1);
  }
  ImGui::SetWindowFontScale(1.0f);

  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  // ImGui::PopStyleVar();
  return 0;
}