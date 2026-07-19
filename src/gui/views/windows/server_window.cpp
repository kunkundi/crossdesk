#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "application/gui_application.h"
#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "rounded_corner_button.h"

namespace crossdesk {

namespace {
int CountDigits(int number) {
  if (number == 0)
    return 1;
  return (int)std::floor(std::log10(std::abs(number))) + 1;
}

void BitrateDisplay(uint32_t bitrate) {
  const int num_of_digits = CountDigits(static_cast<int>(bitrate));
  if (num_of_digits <= 3) {
    ImGui::Text("%u bps", bitrate);
  } else if (num_of_digits > 3 && num_of_digits <= 6) {
    ImGui::Text("%u kbps", bitrate / 1000);
  } else {
    ImGui::Text("%.1f mbps", bitrate / 1000000.0f);
  }
}

std::string FormatBytes(uint64_t bytes) {
  char buf[64];
  if (bytes < 1024ULL) {
    std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
  } else if (bytes < 1024ULL * 1024ULL) {
    std::snprintf(buf, sizeof(buf), "%.2f KB", bytes / 1024.0);
  } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
    std::snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
  } else {
    std::snprintf(buf, sizeof(buf), "%.2f GB",
                  bytes / (1024.0 * 1024.0 * 1024.0));
  }
  return std::string(buf);
}
} // namespace

int GuiApplication::ServerWindow() {
  ImGui::SetNextWindowSize(ImVec2(server_window_width_, server_window_height_),
                           ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
  ImGui::Begin("##server_window", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();

  server_window_title_bar_height_ = title_bar_height_;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::BeginChild(
      "ServerTitleBar",
      ImVec2(server_window_width_, server_window_title_bar_height_),
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);

  float server_title_bar_button_width = server_window_title_bar_height_;
  float server_title_bar_button_height = server_window_title_bar_height_;

  // Collapse/expand toggle button (FontAwesome icon).
  {
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    ImGui::SetWindowFontScale(0.5f);
    const char *icon =
        server_window_collapsed_ ? ICON_FA_ANGLE_DOWN : ICON_FA_ANGLE_UP;
    std::string toggle_label = std::string(icon) + "##server_toggle";

    bool toggle_clicked = RoundedCornerButton(
        toggle_label.c_str(),
        ImVec2(server_title_bar_button_width, server_title_bar_button_height),
        8.5f, ImDrawFlags_RoundCornersTopLeft, true, IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, 25), IM_COL32(255, 255, 255, 255));
    if (toggle_clicked) {
      if (server_window_) {
        int w = 0;
        int h = 0;
        int x = 0;
        int y = 0;
        SDL_GetWindowSize(server_window_, &w, &h);
        SDL_GetWindowPosition(server_window_, &x, &y);

        if (server_window_collapsed_) {
          const int normal_h = server_window_normal_height_;
          SDL_SetWindowSize(server_window_, w, normal_h);
          SDL_SetWindowPosition(server_window_, x, y);
          server_window_collapsed_ = false;
        } else {
          const int collapsed_h = (int)server_window_title_bar_height_;
          // Collapse upward: keep top edge stable.
          SDL_SetWindowSize(server_window_, w, collapsed_h);
          SDL_SetWindowPosition(server_window_, x, y);
          server_window_collapsed_ = true;
        }
      }
    }
    ImGui::SetWindowFontScale(1.0f);

    ImGui::PopStyleColor(3);
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  RemoteClientInfoWindow();

  ImGui::End();
  return 0;
}

int GuiApplication::RemoteClientInfoWindow() {
  float remote_client_info_window_width = server_window_width_ * 0.8f;
  float remote_client_info_window_height =
      (server_window_height_ - server_window_title_bar_height_) * 0.9f;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
  ImGui::BeginChild(
      "RemoteClientInfoWindow",
      ImVec2(remote_client_info_window_width, remote_client_info_window_height),
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  float font_scale = localization_language_index_ == 0 ? 0.5f : 0.45f;

  std::vector<std::pair<std::string, std::string>> remote_entries;
  {
    std::shared_lock lock(connection_status_mutex_);
    remote_entries.reserve(connection_status_.size());
    for (const auto &kv : connection_status_) {
      const auto host_it = connection_host_names_.find(kv.first);
      const std::string display_name =
          (host_it != connection_host_names_.end() && !host_it->second.empty())
              ? host_it->second
              : kv.first;
      remote_entries.emplace_back(kv.first, display_name);
    }
  }

  auto find_display_name_by_remote_id =
      [&remote_entries](const std::string &remote_id) -> std::string {
    for (const auto &entry : remote_entries) {
      if (entry.first == remote_id) {
        return entry.second;
      }
    }
    return {};
  };

  if (!selected_server_remote_id_.empty() &&
      find_display_name_by_remote_id(selected_server_remote_id_).empty()) {
    selected_server_remote_id_.clear();
    selected_server_remote_hostname_.clear();
  }
  if (selected_server_remote_id_.empty() && !remote_entries.empty()) {
    selected_server_remote_id_ = remote_entries.front().first;
  }
  if (!selected_server_remote_id_.empty()) {
    selected_server_remote_hostname_ =
        find_display_name_by_remote_id(selected_server_remote_id_);
  }

  ImGui::SetWindowFontScale(font_scale);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s",
              localization::controller[localization_language_index_].c_str());
  ImGui::SameLine();

  const char *selected_preview = "-";
  if (!selected_server_remote_hostname_.empty()) {
    selected_preview = selected_server_remote_hostname_.c_str();
  } else if (!remote_client_id_.empty()) {
    selected_preview = remote_client_id_.c_str();
  }

  ImGui::SetNextItemWidth(remote_client_info_window_width *
                          (localization_language_index_ == 0 ? 0.68f : 0.62f));
  ImGui::SetWindowFontScale(localization_language_index_ == 0 ? 0.45f : 0.4f);
  ImGui::AlignTextToFramePadding();
  if (ImGui::BeginCombo("##server_remote_id", selected_preview)) {
    ImGui::SetWindowFontScale(localization_language_index_ == 0 ? 0.45f : 0.4f);
    for (int i = 0; i < static_cast<int>(remote_entries.size()); i++) {
      const bool selected =
          (remote_entries[i].first == selected_server_remote_id_);
      if (ImGui::Selectable(remote_entries[i].second.c_str(), selected)) {
        selected_server_remote_id_ = remote_entries[i].first;
        selected_server_remote_hostname_ = remote_entries[i].second;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::Separator();

  ImGui::SetWindowFontScale(font_scale);

  if (!selected_server_remote_id_.empty()) {
    ConnectionStatus status = ConnectionStatus::Closed;
    {
      std::shared_lock lock(connection_status_mutex_);
      auto it = connection_status_.find(selected_server_remote_id_);
      if (it != connection_status_.end()) {
        status = it->second;
      }
    }

    ImGui::Text(
        "%s",
        localization::connection_status[localization_language_index_].c_str());
    ImGui::SameLine();

    switch (status) {
    case ConnectionStatus::Connected:
      ImGui::Text(
          "%s",
          localization::p2p_connected[localization_language_index_].c_str());
      break;
    case ConnectionStatus::Connecting:
      ImGui::Text(
          "%s",
          localization::p2p_connecting[localization_language_index_].c_str());
      break;
    case ConnectionStatus::Disconnected:
      ImGui::Text(
          "%s",
          localization::p2p_disconnected[localization_language_index_].c_str());
      break;
    case ConnectionStatus::Failed:
      ImGui::Text(
          "%s", localization::p2p_failed[localization_language_index_].c_str());
      break;
    case ConnectionStatus::Closed:
      ImGui::Text(
          "%s", localization::p2p_closed[localization_language_index_].c_str());
      break;
    default:
      ImGui::Text(
          "%s", localization::p2p_failed[localization_language_index_].c_str());
      break;
    }
  }

  ImGui::Separator();

  ImGui::AlignTextToFramePadding();
  ImGui::Text(
      "%s", localization::file_transfer[localization_language_index_].c_str());

  ImGui::SameLine();

  if (ImGui::Button(
          localization::select_file[localization_language_index_].c_str())) {
    std::string title = localization::select_file[localization_language_index_];
    std::string path = OpenFileDialog(title);
    LOG_INFO("Selected file path: {}", path.c_str());

    transfers_.ProcessSelectedFile(path, nullptr, file_label_,
                                   selected_server_remote_id_);
  }

  auto &file_transfer = transfers_.global_state();
  if (file_transfer.file_transfer_window_visible_) {
    ImGui::SameLine();
    const bool is_sending = file_transfer.file_sending_.load();

    if (is_sending) {
      // Simple animation: cycle icon every 0.5s while sending.
      static const char *kFileTransferIcons[] = {ICON_FA_ANGLE_UP,
                                                 ICON_FA_ANGLES_UP};
      const int icon_index = static_cast<int>(ImGui::GetTime() / 0.5) %
                             (static_cast<int>(sizeof(kFileTransferIcons) /
                                               sizeof(kFileTransferIcons[0])));
      ImGui::Text("%s", kFileTransferIcons[icon_index]);
    } else {
      // Completed.
      ImGui::Text("%s", ICON_FA_CHECK);
    }

    if (ImGui::IsItemHovered()) {
      const uint64_t sent_bytes = file_transfer.file_sent_bytes_.load();
      const uint64_t total_bytes = file_transfer.file_total_bytes_.load();
      const uint32_t rate_bps = file_transfer.file_send_rate_bps_.load();

      float progress = 0.0f;
      if (total_bytes > 0) {
        progress =
            static_cast<float>(sent_bytes) / static_cast<float>(total_bytes);
        progress = (std::max)(0.0f, (std::min)(1.0f, progress));
      }

      std::string current_file_name;
      const uint32_t current_file_id = file_transfer.current_file_id_.load();
      if (current_file_id != 0) {
        std::lock_guard<std::mutex> lock(
            file_transfer.file_transfer_list_mutex_);
        for (const auto &info : file_transfer.file_transfer_list_) {
          if (info.file_id == current_file_id) {
            current_file_name = info.file_name;
            break;
          }
        }
      }

      ImGui::BeginTooltip();
      if (server_windows_system_chinese_font_) {
        ImGui::PushFont(server_windows_system_chinese_font_);
      }
      ImGui::SetWindowFontScale(0.5f);
      if (!current_file_name.empty()) {
        ImGui::Text("%s", current_file_name.c_str());
      }
      if (total_bytes > 0) {
        const std::string sent_str = FormatBytes(sent_bytes);
        const std::string total_str = FormatBytes(total_bytes);
        ImGui::Text("%s / %s", sent_str.c_str(), total_str.c_str());
      }

      const float text_height = ImGui::GetTextLineHeight();
      char overlay[32];
      std::snprintf(overlay, sizeof(overlay), "%.1f%%", progress * 100.0f);
      ImGui::ProgressBar(progress, ImVec2(180.0f, text_height), overlay);
      BitrateDisplay(rate_bps);
      ImGui::SetWindowFontScale(1.0f);
      if (server_windows_system_chinese_font_) {
        ImGui::PopFont();
      }
      ImGui::EndTooltip();
    }
  }

  ImGui::SetWindowFontScale(1.0f);

  ImGui::EndChild();

  ImGui::SameLine();

  float close_connection_button_width = server_window_width_ * 0.1f;
  float close_connection_button_height = remote_client_info_window_height;

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_);
  ImGui::SetWindowFontScale(font_scale);
  if (ImGui::Button(ICON_FA_XMARK, ImVec2(close_connection_button_width,
                                          close_connection_button_height))) {
    if (peer_ && !selected_server_remote_id_.empty()) {
      LeaveConnection(peer_, selected_server_remote_id_.c_str());
    }
  }
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();

  return 0;
}
} // namespace crossdesk
