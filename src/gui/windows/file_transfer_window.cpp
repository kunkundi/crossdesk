#include <algorithm>
#include <cmath>

#include "IconsFontAwesome6.h"
#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {

namespace {
int CountDigits(int number) {
  if (number == 0) return 1;
  return (int)std::floor(std::log10(std::abs(number))) + 1;
}

int BitrateDisplay(int bitrate) {
  int num_of_digits = CountDigits(bitrate);
  if (num_of_digits <= 3) {
    ImGui::Text("%d bps", bitrate);
  } else if (num_of_digits > 3 && num_of_digits <= 6) {
    ImGui::Text("%d kbps", bitrate / 1000);
  } else {
    ImGui::Text("%.1f mbps", bitrate / 1000000.0f);
  }
  return 0;
}
}  // namespace

int Render::FileTransferWindow(
    std::shared_ptr<SubStreamWindowProperties>& props) {
  if (!props->file_transfer_window_visible_ &&
      !props->file_transfer_completed_) {
    return 0;
  }

  ImGuiIO& io = ImGui::GetIO();

  // Position window at bottom-left of stream window
  float window_width = main_window_width_ * 0.35f;
  float window_height = main_window_height_ * 0.25f;
  float pos_x = 10.0f;
  float pos_y = 10.0f;

  // Get stream window size and position
  ImVec2 stream_window_size =
      ImVec2(stream_window_width_, stream_window_height_);
  if (fullscreen_button_pressed_) {
    pos_y = stream_window_size.y - window_height - 10.0f;
  } else {
    pos_y = stream_window_size.y - window_height - 10.0f - title_bar_height_;
  }

  ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
                           ImGuiCond_Always);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.8f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

  std::string window_title = "File Transfer";
  bool window_opened = true;

  // ImGui::SetWindowFontScale(0.5f);
  if (ImGui::Begin("FileTransferWindow", &window_opened,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(0.5f);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    // Close button handling
    if (!window_opened) {
      props->file_transfer_window_visible_ = false;
      props->file_transfer_completed_ = false;
      ImGui::End();
      return 0;
    }

    bool is_sending = props->file_sending_.load();
    uint64_t total = props->file_total_bytes_.load();
    bool has_transfer = total > 0;  // Check if there's an active transfer

    if (props->file_transfer_completed_ && !is_sending) {
      // Show completion message
      ImGui::SetCursorPos(ImVec2(10, 30));
      ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f),
                         "%s File transfer completed!", ICON_FA_CHECK);

      std::string file_name;
      {
        std::lock_guard<std::mutex> lock(props->file_transfer_mutex_);
        file_name = props->file_sending_name_;
      }
      if (!file_name.empty()) {
        ImGui::SetCursorPos(ImVec2(10, 50));
        ImGui::Text("File: %s", file_name.c_str());
      }

      ImGui::SetCursorPos(ImVec2(10, 70));
      if (ImGui::Button("OK", ImVec2(80, 25))) {
        props->file_transfer_completed_ = false;
        props->file_transfer_window_visible_ = false;
      }
    } else if (has_transfer && !props->file_transfer_completed_) {
      // Show transfer progress (either sending or waiting for ACK)
      uint64_t sent = props->file_sent_bytes_.load();
      // Re-read total in case it was updated
      uint64_t current_total = props->file_total_bytes_.load();
      float progress = current_total > 0 ? static_cast<float>(sent) /
                                               static_cast<float>(current_total)
                                         : 0.0f;
      progress = (std::max)(0.0f, (std::min)(1.0f, progress));

      // File name
      std::string file_name;
      {
        std::lock_guard<std::mutex> lock(props->file_transfer_mutex_);
        file_name = props->file_sending_name_;
      }
      if (file_name.empty()) {
        file_name = "Sending...";
      }

      ImGui::SetCursorPos(ImVec2(10, 30));
      ImGui::Text("File: %s", file_name.c_str());

      // Progress bar
      ImGui::SetCursorPos(ImVec2(10, 50));
      ImGui::ProgressBar(progress, ImVec2(window_width - 40, 0), "");
      ImGui::SameLine(0, 5);
      ImGui::Text("%.1f%%", progress * 100.0f);

      // Transfer rate and size info
      ImGui::SetCursorPos(ImVec2(10, 75));
      uint32_t rate_bps = props->file_send_rate_bps_.load();
      ImGui::Text("Speed: ");
      ImGui::SameLine();
      BitrateDisplay(static_cast<int>(rate_bps));

      ImGui::SameLine(0, 20);
      // Format size display
      char size_str[64];
      if (current_total < 1024) {
        snprintf(size_str, sizeof(size_str), "%llu B",
                 (unsigned long long)current_total);
      } else if (current_total < 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%.2f KB",
                 current_total / 1024.0f);
      } else {
        snprintf(size_str, sizeof(size_str), "%.2f MB",
                 current_total / (1024.0f * 1024.0f));
      }
      ImGui::Text("Size: %s", size_str);
    }

    ImGui::End();
  } else {
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
  }
  ImGui::SetWindowFontScale(1.0f);

  return 0;
}

}  // namespace crossdesk
