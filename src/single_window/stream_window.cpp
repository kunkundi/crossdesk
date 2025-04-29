#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::StreamWindow() {
  ImGui::SetNextWindowPos(
      ImVec2(0, fullscreen_button_pressed_ ? 0 : title_bar_height_),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(stream_window_width_, stream_window_height_),
                           ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::Begin("VideoBg", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoDocking);

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();

  ImGuiTabBarFlags tab_bar_flags =
      ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs;
  ImGuiWindowFlags stream_window_flag =
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove;

  ImGui::SetNextWindowPos(ImVec2(20, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(0, 20), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.0f));
  ImGui::Begin("TabBar", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  if (ImGui::BeginTabBar("StreamTabBar", tab_bar_flags)) {
    if (ImGui::IsWindowHovered()) {
      is_tab_bar_hovered_ = true;
    } else {
      is_tab_bar_hovered_ = false;
    }

    for (auto it = client_properties_.begin(); it != client_properties_.end();
         ++it) {
      auto& props = it->second;

      ImGui::SetWindowFontScale(0.6f);
      if (props->tab_opened_) {
        if (ImGui::BeginTabItem(props->remote_id_.c_str(), &props->tab_opened_,
                                ImGuiTabItemFlags_None)) {
          props->tab_selected_ = true;
          ImGui::SetWindowFontScale(1.0f);
          ImGui::SetNextWindowSize(
              ImVec2(stream_window_width_, stream_window_height_),
              ImGuiCond_Once);
          ImGui::SetNextWindowPos(
              ImVec2(0, fullscreen_button_pressed_ ? 0 : title_bar_height_),
              ImGuiCond_Appearing);
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
          ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
          ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.0f));
          ImGui::Begin(props->remote_id_.c_str(), nullptr, stream_window_flag);
          ImGui::PopStyleColor();
          ImGui::PopStyleVar(2);

          ImVec2 stream_window_pos = ImGui::GetWindowPos();
          ImGuiViewport* viewport = ImGui::GetWindowViewport();
          ImVec2 stream_window_size = ImGui::GetWindowSize();
          props->render_window_x_ = stream_window_pos.x;
          props->render_window_y_ = stream_window_pos.y;
          props->render_window_width_ = stream_window_size.x;
          props->render_window_height_ = stream_window_size.y;

          ControlWindow(props);

          ImGui::End();
          ImGui::EndTabItem();
        } else {
          props->tab_selected_ = false;
        }
      } else {
        CleanupPeer(it->first, props);
        it = client_properties_.erase(it);
        if (client_properties_.empty()) {
          SDL_Event event;
          event.type = SDL_QUIT;
          SDL_PushEvent(&event);
        }
      }
    }

    ImGui::EndTabBar();
  }
  ImGui::End();

  UpdateRenderRect();

  ImGui::End();

  return 0;
}