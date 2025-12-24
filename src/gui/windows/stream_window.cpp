#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

void Render::DrawConnectionStatusText(
    std::shared_ptr<SubStreamWindowProperties>& props) {
  std::string text;
  switch (props->connection_status_) {
    case ConnectionStatus::Disconnected:
      text = localization::p2p_disconnected[localization_language_index_];
      break;
    case ConnectionStatus::Failed:
      text = localization::p2p_failed[localization_language_index_];
      break;
    case ConnectionStatus::Closed:
      text = localization::p2p_closed[localization_language_index_];
      break;
    default:
      break;
  }

  if (!text.empty()) {
    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    ImGui::SetCursorPos(
        ImVec2((size.x - text_size.x) * 0.5f,
               (size.y - text_size.y - title_bar_height_) * 0.5f));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", text.c_str());
  }
}

void Render::CloseTab(decltype(client_properties_)::iterator& it) {
  // std::unique_lock lock(client_properties_mutex_);
  if (it != client_properties_.end()) {
    CleanupPeer(it->second);
    it = client_properties_.erase(it);
    if (client_properties_.empty()) {
      SDL_Event event;
      event.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&event);
    }
  }
}

int Render::StreamWindow() {
  ImGui::SetNextWindowPos(
      ImVec2(0, fullscreen_button_pressed_ ? 0 : title_bar_height_),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(stream_window_width_, stream_window_height_),
                           ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  bool video_bg_opened = ImGui::Begin(
      "VideoBg", nullptr,
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();

  if (!video_bg_opened) {
    return 0;
  }

  ImGuiWindowFlags stream_window_flag =
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove;

  if (!fullscreen_button_pressed_) {
    ImGui::SetNextWindowPos(
        ImVec2(title_bar_button_width_ * 0.8f, title_bar_button_width_ * 0.1f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(0, title_bar_button_width_ * 0.8f),
                             ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.0f));
    ImGui::Begin("TabBar", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (ImGui::BeginTabBar("StreamTabBar",
                           ImGuiTabBarFlags_Reorderable |
                               ImGuiTabBarFlags_AutoSelectNewTabs)) {
      is_tab_bar_hovered_ = ImGui::IsWindowHovered();

      // std::shared_lock lock(client_properties_mutex_);
      for (auto it = client_properties_.begin();
           it != client_properties_.end();) {
        auto& props = it->second;
        if (!props->tab_opened_) {
          std::string remote_id_to_close = props->remote_id_;
          // lock.unlock();
          {
            // std::unique_lock unique_lock(client_properties_mutex_);
            auto close_it = client_properties_.find(remote_id_to_close);
            if (close_it != client_properties_.end()) {
              CloseTab(close_it);
            }
          }
          // lock.lock();
          it = client_properties_.begin();
          continue;
        }

        std::string tab_label =
            enable_srtp_
                ? std::string(ICON_FA_SHIELD_HALVED) + " " + props->remote_id_
                : props->remote_id_;
        if (ImGui::BeginTabItem(tab_label.c_str(), &props->tab_opened_)) {
          props->tab_selected_ = true;
          ImGui::SetWindowFontScale(0.6f);

          ImGui::SetNextWindowSize(
              ImVec2(stream_window_width_, stream_window_height_),
              ImGuiCond_Always);
          ImGui::SetNextWindowPos(
              ImVec2(0, fullscreen_button_pressed_ ? 0 : title_bar_height_),
              ImGuiCond_Always);
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
          ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
          ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.0f));
          ImGui::Begin(props->remote_id_.c_str(), nullptr, stream_window_flag);
          ImGui::PopStyleColor();
          ImGui::PopStyleVar(2);

          ImVec2 pos = ImGui::GetWindowPos();
          ImVec2 size = ImGui::GetWindowSize();
          props->render_window_x_ = pos.x;
          props->render_window_y_ = pos.y;
          props->render_window_width_ = size.x;
          props->render_window_height_ = size.y;
          UpdateRenderRect();

          ControlWindow(props);
          
          // Show file transfer window if needed
          FileTransferWindow(props);

          focused_remote_id_ = props->remote_id_;

          if (!props->peer_) {
            std::string remote_id_to_erase = props->remote_id_;
            // lock.unlock();
            {
              // std::unique_lock unique_lock(client_properties_mutex_);
              auto erase_it = client_properties_.find(remote_id_to_erase);
              if (erase_it != client_properties_.end()) {
                erase_it = client_properties_.erase(erase_it);
                if (client_properties_.empty()) {
                  SDL_Event event;
                  event.type = SDL_EVENT_QUIT;
                  SDL_PushEvent(&event);
                }
              }
            }
            // lock.lock();
            ImGui::End();
            ImGui::EndTabItem();
            it = client_properties_.begin();
            continue;
          } else {
            DrawConnectionStatusText(props);
            ++it;
          }

          ImGui::End();
          ImGui::EndTabItem();
        } else {
          props->tab_selected_ = false;
          if (!props->tab_opened_) {
            std::string remote_id_to_close = props->remote_id_;
            // lock.unlock();
            {
              // std::unique_lock unique_lock(client_properties_mutex_);
              auto close_it = client_properties_.find(remote_id_to_close);
              if (close_it != client_properties_.end()) {
                CloseTab(close_it);
              }
            }
            // lock.lock();
            it = client_properties_.begin();
            continue;
          }
          ++it;
        }
      }

      ImGui::EndTabBar();
    }

    ImGui::End();  // End TabBar
  } else {
    // std::shared_lock lock(client_properties_mutex_);
    for (auto it = client_properties_.begin();
         it != client_properties_.end();) {
      auto& props = it->second;
      if (!props->tab_opened_) {
        std::string remote_id_to_close = props->remote_id_;
        // lock.unlock();
        {
          // std::unique_lock unique_lock(client_properties_mutex_);
          auto close_it = client_properties_.find(remote_id_to_close);
          if (close_it != client_properties_.end()) {
            CloseTab(close_it);
          }
        }
        // lock.lock();
        it = client_properties_.begin();
        continue;
      }

      if (props->tab_selected_) {
        ImGui::SetNextWindowSize(
            ImVec2(stream_window_width_, stream_window_height_),
            ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.0f));
        ImGui::Begin(props->remote_id_.c_str(), nullptr, stream_window_flag);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        props->render_window_x_ = pos.x;
        props->render_window_y_ = pos.y;
        props->render_window_width_ = size.x;
        props->render_window_height_ = size.y;
        UpdateRenderRect();

        ControlWindow(props);
        
        // Show file transfer window if needed
        FileTransferWindow(props);
        
        ImGui::End();

        if (!props->peer_) {
          fullscreen_button_pressed_ = false;
          SDL_SetWindowFullscreen(stream_window_, false);
          std::string remote_id_to_erase = props->remote_id_;
          // lock.unlock();
          {
            // std::unique_lock unique_lock(client_properties_mutex_);
            auto erase_it = client_properties_.find(remote_id_to_erase);
            if (erase_it != client_properties_.end()) {
              client_properties_.erase(erase_it);
              if (client_properties_.empty()) {
                SDL_Event event;
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
              }
            }
          }
          // lock.lock();
          it = client_properties_.begin();
          continue;
        } else {
          DrawConnectionStatusText(props);
          ++it;
        }
      } else {
        ++it;
      }
    }
  }

  // UpdateRenderRect();
  ImGui::End();  // End VideoBg

  return 0;
}
}  // namespace crossdesk