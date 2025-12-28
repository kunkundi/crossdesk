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
  // Only show window if there are files in transfer list or currently
  // transferring
  std::vector<SubStreamWindowProperties::FileTransferInfo> file_list;
  {
    std::lock_guard<std::mutex> lock(props->file_transfer_list_mutex_);
    file_list = props->file_transfer_list_;
  }

  // Sort file list: Sending first, then Completed, then Queued, then Failed
  std::sort(
      file_list.begin(), file_list.end(),
      [](const SubStreamWindowProperties::FileTransferInfo& a,
         const SubStreamWindowProperties::FileTransferInfo& b) {
        // Priority: Sending > Completed > Queued > Failed
        auto get_priority =
            [](SubStreamWindowProperties::FileTransferStatus status) {
              switch (status) {
                case SubStreamWindowProperties::FileTransferStatus::Sending:
                  return 0;
                case SubStreamWindowProperties::FileTransferStatus::Completed:
                  return 1;
                case SubStreamWindowProperties::FileTransferStatus::Queued:
                  return 2;
                case SubStreamWindowProperties::FileTransferStatus::Failed:
                  return 3;
              }
              return 3;
            };
        return get_priority(a.status) < get_priority(b.status);
      });

  // Only show window if file_transfer_window_visible_ is true
  // Window can be closed by user even during transfer
  // It will be reopened automatically when:
  // 1. A file transfer completes (in render_callback.cpp)
  // 2. A new file starts sending from queue (in render.cpp)
  if (!props->file_transfer_window_visible_) {
    return 0;
  }

  ImGuiIO& io = ImGui::GetIO();

  // Position window at bottom-left of stream window
  // Adjust window size based on number of files
  float file_transfer_window_width = main_window_width_ * 0.6f;
  float file_transfer_window_height =
      main_window_height_ * 0.3f;  // Dynamic height
  float pos_x = file_transfer_window_width * 0.05f;
  float pos_y = stream_window_height_ - file_transfer_window_height -
                file_transfer_window_width * 0.05;
  float same_line_width = file_transfer_window_width * 0.1f;

  ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
  ImGui::SetNextWindowSize(
      ImVec2(file_transfer_window_width, file_transfer_window_height),
      ImGuiCond_Always);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

  ImGui::SetWindowFontScale(0.5f);
  bool window_opened = true;
  if (ImGui::Begin(
          localization::file_transfer_progress[localization_language_index_]
              .c_str(),
          &window_opened,
          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
              ImGuiWindowFlags_NoScrollbar)) {
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetWindowFontScale(0.5f);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    // Close button handling
    if (!window_opened) {
      props->file_transfer_window_visible_ = false;
      ImGui::End();
      return 0;
    }

    // Display file list
    if (file_list.empty()) {
      ImGui::Text("No files in transfer queue");
    } else {
      // Use a scrollable child window for the file list
      ImGui::SetWindowFontScale(0.5f);
      ImGui::BeginChild("FileList",
                        ImVec2(0, file_transfer_window_height * 0.75f),
                        ImGuiChildFlags_Border);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetWindowFontScale(0.5f);

      for (size_t i = 0; i < file_list.size(); ++i) {
        const auto& info = file_list[i];
        ImGui::PushID(static_cast<int>(i));

        // Status icon and file name
        const char* status_icon = "";
        ImVec4 status_color(0.5f, 0.5f, 0.5f, 1.0f);
        const char* status_text = "";

        switch (info.status) {
          case SubStreamWindowProperties::FileTransferStatus::Queued:
            status_icon = ICON_FA_CLOCK;
            status_color =
                ImVec4(0.5f, 0.6f, 0.7f, 1.0f);  // Common blue-gray for queued
            status_text =
                localization::queued[localization_language_index_].c_str();
            break;
          case SubStreamWindowProperties::FileTransferStatus::Sending:
            status_icon = ICON_FA_ARROW_UP;
            status_color = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
            status_text =
                localization::sending[localization_language_index_].c_str();
            break;
          case SubStreamWindowProperties::FileTransferStatus::Completed:
            status_icon = ICON_FA_CHECK;
            status_color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
            status_text =
                localization::completed[localization_language_index_].c_str();
            break;
          case SubStreamWindowProperties::FileTransferStatus::Failed:
            status_icon = ICON_FA_XMARK;
            status_color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            status_text =
                localization::failed[localization_language_index_].c_str();
            break;
        }

        ImGui::TextColored(status_color, "%s", status_icon);
        ImGui::SameLine();
        ImGui::Text("%s", info.file_name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(status_color, "%s", status_text);

        // Progress bar for sending files
        if (info.status ==
                SubStreamWindowProperties::FileTransferStatus::Sending &&
            info.file_size > 0) {
          float progress = static_cast<float>(info.sent_bytes) /
                           static_cast<float>(info.file_size);
          progress = (std::max)(0.0f, (std::min)(1.0f, progress));

          float text_height = ImGui::GetTextLineHeight();
          ImGui::ProgressBar(
              progress, ImVec2(file_transfer_window_width * 0.5f, text_height),
              "");
          ImGui::SameLine();

          ImGui::Text("%.1f%%", progress * 100.0f);
          ImGui::SameLine();

          float speed_x_pos = file_transfer_window_width * 0.65f;
          ImGui::SetCursorPosX(speed_x_pos);
          BitrateDisplay(static_cast<int>(info.rate_bps));
        } else if (info.status ==
                   SubStreamWindowProperties::FileTransferStatus::Completed) {
          // Show completed size
          char size_str[64];
          if (info.file_size < 1024) {
            snprintf(size_str, sizeof(size_str), "%llu B",
                     (unsigned long long)info.file_size);
          } else if (info.file_size < 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.2f KB",
                     info.file_size / 1024.0f);
          } else {
            snprintf(size_str, sizeof(size_str), "%.2f MB",
                     info.file_size / (1024.0f * 1024.0f));
          }
          ImGui::Text("Size: %s", size_str);
        }

        ImGui::PopID();
        ImGui::Spacing();
      }

      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetWindowFontScale(0.5f);
      ImGui::EndChild();
      ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetWindowFontScale(0.5f);
    ImGui::End();
    ImGui::SetWindowFontScale(1.0f);
  } else {
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
  }
  ImGui::SetWindowFontScale(1.0f);

  return 0;
}

}  // namespace crossdesk
