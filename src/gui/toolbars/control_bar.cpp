#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "file_transfer.h"
#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"
#include "tinyfiledialogs.h"

namespace crossdesk {

std::string OpenFileDialog(std::string title) {
  const char* path = tinyfd_openFileDialog(title.c_str(),
                                           "",       // default path
                                           0,        // number of filters
                                           nullptr,  // filters
                                           nullptr,  // filter description
                                           0         // no multiple selection
  );

  return path ? path : "";
}

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

int LossRateDisplay(float loss_rate) {
  if (loss_rate < 0.01f) {
    ImGui::Text("0%%");
  } else {
    ImGui::Text("%.0f%%", loss_rate * 100);
  }
  return 0;
}

int Render::ControlBar(std::shared_ptr<SubStreamWindowProperties>& props) {
  float button_width = title_bar_height_ * 0.8f;
  float button_height = title_bar_height_ * 0.8f;
  float line_padding = title_bar_height_ * 0.12f;
  float line_thickness = title_bar_height_ * 0.07f;

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
  if (props->control_bar_expand_) {
    ImGui::SetCursorPosX(props->is_control_bar_in_left_
                             ? props->control_window_width_ * 1.03f
                             : props->control_window_width_ * 0.17f);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (!props->is_control_bar_in_left_) {
      draw_list->AddLine(
          ImVec2(ImGui::GetCursorScreenPos().x - button_height * 0.5f,
                 ImGui::GetCursorScreenPos().y + button_height * 0.2f),
          ImVec2(ImGui::GetCursorScreenPos().x - button_height * 0.5f,
                 ImGui::GetCursorScreenPos().y + button_height * 0.8f),
          IM_COL32(178, 178, 178, 255), 2.0f);
    }

    std::string display = ICON_FA_DISPLAY;
    ImGui::SetWindowFontScale(0.5f);
    if (ImGui::Button(display.c_str(), ImVec2(button_width, button_height))) {
      ImGui::OpenPopup("display");
    }

    ImVec2 btn_min = ImGui::GetItemRectMin();
    ImVec2 btn_size_actual = ImGui::GetItemRectSize();

    if (ImGui::BeginPopup("display")) {
      ImGui::SetWindowFontScale(0.5f);
      for (int i = 0; i < props->display_info_list_.size(); i++) {
        if (ImGui::Selectable(props->display_info_list_[i].name.c_str())) {
          props->selected_display_ = i;

          RemoteAction remote_action;
          remote_action.type = ControlType::display_id;
          remote_action.d = i;
          if (props->connection_status_ == ConnectionStatus::Connected) {
            std::string msg = remote_action.to_json();
            SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                          props->data_label_.c_str());
          }
        }
        props->display_selectable_hovered_ = ImGui::IsWindowHovered();
      }
      ImGui::EndPopup();
    }

    ImGui::SetWindowFontScale(0.5f);
    ImVec2 text_size = ImGui::CalcTextSize(
        std::to_string(props->selected_display_ + 1).c_str());
    ImVec2 text_pos =
        ImVec2(btn_min.x + (btn_size_actual.x - text_size.x) * 0.5f,
               btn_min.y + (btn_size_actual.y - text_size.y) * 0.35f);
    ImGui::GetWindowDrawList()->AddText(
        text_pos, IM_COL32(0, 0, 0, 255),
        std::to_string(props->selected_display_ + 1).c_str());

    ImGui::SameLine();
    float mouse_x = ImGui::GetCursorScreenPos().x;
    float mouse_y = ImGui::GetCursorScreenPos().y;
    float disable_mouse_x = mouse_x + line_padding;
    float disable_mouse_y = mouse_y + line_padding;
    std::string mouse = props->mouse_control_button_pressed_
                            ? ICON_FA_COMPUTER_MOUSE
                            : ICON_FA_COMPUTER_MOUSE;
    ImGui::SetWindowFontScale(0.5f);
    if (ImGui::Button(mouse.c_str(), ImVec2(button_width, button_height))) {
      if (props->connection_established_) {
        start_keyboard_capturer_ = !start_keyboard_capturer_;
        props->control_mouse_ = !props->control_mouse_;
        props->mouse_control_button_pressed_ =
            !props->mouse_control_button_pressed_;
        props->mouse_control_button_label_ =
            props->mouse_control_button_pressed_
                ? localization::release_mouse[localization_language_index_]
                : localization::control_mouse[localization_language_index_];
      }
    }

    if (!props->mouse_control_button_pressed_) {
      draw_list->AddLine(ImVec2(disable_mouse_x, disable_mouse_y),
                         ImVec2(mouse_x + button_width - line_padding,
                                mouse_y + button_height - line_padding),
                         IM_COL32(0, 0, 0, 255), line_thickness);
      draw_list->AddLine(
          ImVec2(disable_mouse_x - line_thickness * 0.7f,
                 disable_mouse_y + line_thickness * 0.7f),
          ImVec2(
              mouse_x + button_width - line_padding - line_thickness * 0.7f,
              mouse_y + button_height - line_padding + line_thickness * 0.7f),
          ImGui::IsItemHovered() ? IM_COL32(66, 150, 250, 255)
                                 : IM_COL32(179, 213, 253, 255),
          line_thickness);
    }

    ImGui::SameLine();
    // audio capture button
    float audio_x = ImGui::GetCursorScreenPos().x;
    float audio_y = ImGui::GetCursorScreenPos().y;
    float disable_audio_x = audio_x + line_padding;
    float disable_audio_y = audio_y + line_padding;

    std::string audio = props->audio_capture_button_pressed_
                            ? ICON_FA_VOLUME_HIGH
                            : ICON_FA_VOLUME_HIGH;
    ImGui::SetWindowFontScale(0.5f);
    if (ImGui::Button(audio.c_str(), ImVec2(button_width, button_height))) {
      if (props->connection_established_) {
        props->audio_capture_button_pressed_ =
            !props->audio_capture_button_pressed_;
        props->audio_capture_button_label_ =
            props->audio_capture_button_pressed_
                ? localization::audio_capture[localization_language_index_]
                : localization::mute[localization_language_index_];

        RemoteAction remote_action;
        remote_action.type = ControlType::audio_capture;
        remote_action.a = props->audio_capture_button_pressed_;
        std::string msg = remote_action.to_json();
        SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                      props->data_label_.c_str());
      }
    }

    if (!props->audio_capture_button_pressed_) {
      draw_list->AddLine(ImVec2(disable_audio_x, disable_audio_y),
                         ImVec2(audio_x + button_width - line_padding,
                                audio_y + button_height - line_padding),
                         IM_COL32(0, 0, 0, 255), line_thickness);
      draw_list->AddLine(
          ImVec2(disable_audio_x - line_thickness * 0.7f,
                 disable_audio_y + line_thickness * 0.7f),
          ImVec2(
              audio_x + button_width - line_padding - line_thickness * 0.7f,
              audio_y + button_height - line_padding + line_thickness * 0.7f),
          ImGui::IsItemHovered() ? IM_COL32(66, 150, 250, 255)
                                 : IM_COL32(179, 213, 253, 255),
          line_thickness);
    }

    ImGui::SameLine();
    std::string open_folder = ICON_FA_FOLDER_OPEN;
    if (ImGui::Button(open_folder.c_str(),
                      ImVec2(button_width, button_height))) {
      std::string title =
          localization::select_file[localization_language_index_];
      std::string path = OpenFileDialog(title);
      if (!path.empty()) {
        LOG_INFO("Selected file: {}", path.c_str());

        // Send selected file over file data channel in a background thread.
        auto peer = props->peer_;
        std::filesystem::path file_path = std::filesystem::path(path);
        std::string file_label = file_label_;

        std::thread([peer, file_path, file_label]() {
          FileSender sender;
          int ret = sender.SendFile(
              file_path, file_path.filename().string(),
              [peer, file_label](const char* buf, size_t sz) -> int {
                return SendReliableDataFrame(peer, buf, sz, file_label.c_str());
              });

          if (ret != 0) {
            LOG_ERROR("FileSender::SendFile failed for [{}], ret={}",
                      file_path.string().c_str(), ret);
          } else {
            LOG_INFO("File send finished: {}", file_path.string().c_str());
          }
        }).detach();
      }
    }

    ImGui::SameLine();
    // net traffic stats button
    bool button_color_style_pushed = false;
    if (props->net_traffic_stats_button_pressed_) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(66 / 255.0f, 150 / 255.0f,
                                                    250 / 255.0f, 1.0f));
      button_color_style_pushed = true;
    }
    std::string net_traffic_stats = ICON_FA_SIGNAL;
    ImGui::SetWindowFontScale(0.5f);
    if (ImGui::Button(net_traffic_stats.c_str(),
                      ImVec2(button_width, button_height))) {
      props->net_traffic_stats_button_pressed_ =
          !props->net_traffic_stats_button_pressed_;
      props->control_window_height_is_changing_ = true;
      props->net_traffic_stats_button_pressed_time_ = ImGui::GetTime();
      props->net_traffic_stats_button_label_ =
          props->net_traffic_stats_button_pressed_
              ? localization::hide_net_traffic_stats
                    [localization_language_index_]
              : localization::show_net_traffic_stats
                    [localization_language_index_];
    }

    if (button_color_style_pushed) {
      ImGui::PopStyleColor();
      button_color_style_pushed = false;
    }

    ImGui::SameLine();
    // fullscreen button
    std::string fullscreen =
        fullscreen_button_pressed_ ? ICON_FA_COMPRESS : ICON_FA_EXPAND;
    ImGui::SetWindowFontScale(0.5f);
    if (ImGui::Button(fullscreen.c_str(),
                      ImVec2(button_width, button_height))) {
      fullscreen_button_pressed_ = !fullscreen_button_pressed_;
      props->fullscreen_button_label_ =
          fullscreen_button_pressed_
              ? localization::exit_fullscreen[localization_language_index_]
              : localization::fullscreen[localization_language_index_];

      if (fullscreen_button_pressed_) {
        SDL_SetWindowFullscreen(stream_window_, true);
      } else {
        SDL_SetWindowFullscreen(stream_window_, false);
      }
      props->reset_control_bar_pos_ = true;
    }

    ImGui::SameLine();
    // close button
    std::string close_button = ICON_FA_XMARK;
    ImGui::SetWindowFontScale(0.5f);
    if (ImGui::Button(close_button.c_str(),
                      ImVec2(button_width, button_height))) {
      CleanupPeer(props);
    }

    ImGui::SameLine();

    if (props->is_control_bar_in_left_) {
      draw_list->AddLine(
          ImVec2(ImGui::GetCursorScreenPos().x + button_height * 0.13f,
                 ImGui::GetCursorScreenPos().y + button_height * 0.2f),
          ImVec2(ImGui::GetCursorScreenPos().x + button_height * 0.13f,
                 ImGui::GetCursorScreenPos().y + button_height * 0.8f),
          IM_COL32(178, 178, 178, 255), 2.0f);
    }

    ImGui::SameLine();
  }

  float expand_button_pos_x =
      props->control_bar_expand_ ? (props->is_control_bar_in_left_
                                        ? props->control_window_width_ * 1.917f
                                        : props->control_window_width_ * 0.03f)
                                 : (props->is_control_bar_in_left_
                                        ? props->control_window_width_ * 1.02f
                                        : props->control_window_width_ * 0.23f);

  ImGui::SetCursorPosX(expand_button_pos_x);

  std::string control_bar =
      props->control_bar_expand_
          ? (props->is_control_bar_in_left_ ? ICON_FA_ANGLE_LEFT
                                            : ICON_FA_ANGLE_RIGHT)
          : (props->is_control_bar_in_left_ ? ICON_FA_ANGLE_RIGHT
                                            : ICON_FA_ANGLE_LEFT);
  if (ImGui::Button(control_bar.c_str(),
                    ImVec2(button_height * 0.6f, button_height))) {
    props->control_bar_expand_ = !props->control_bar_expand_;
    props->control_bar_button_pressed_time_ = ImGui::GetTime();
    props->control_window_width_is_changing_ = true;

    if (!props->control_bar_expand_) {
      props->control_window_height_ = props->control_window_min_height_;
      props->net_traffic_stats_button_pressed_ = false;
    }
  }

  if (props->net_traffic_stats_button_pressed_ && props->control_bar_expand_) {
    NetTrafficStats(props);
  }

  ImGui::PopStyleVar();

  return 0;
}

int Render::NetTrafficStats(std::shared_ptr<SubStreamWindowProperties>& props) {
  ImGui::SetCursorPos(ImVec2(props->is_control_bar_in_left_
                                 ? props->control_window_width_ * 1.02f
                                 : props->control_window_width_ * 0.02f,
                             props->control_window_min_height_));
  ImGui::SetWindowFontScale(0.5f);
  if (ImGui::BeginTable("NetTrafficStats", 4, ImGuiTableFlags_BordersH,
                        ImVec2(props->control_window_max_width_ * 0.9f,
                               props->control_window_max_height_ - 0.9f))) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);

    ImGui::TableNextColumn();
    ImGui::Text(" ");
    ImGui::TableNextColumn();
    ImGui::Text("%s", localization::in[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s", localization::out[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::loss_rate[localization_language_index_].c_str());

    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::video[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.video_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.video_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(props->net_traffic_stats_.video_inbound_stats.loss_rate);

    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::audio[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.audio_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.audio_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(props->net_traffic_stats_.audio_inbound_stats.loss_rate);

    ImGui::TableNextColumn();
    ImGui::Text("%s", localization::data[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.data_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.data_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(props->net_traffic_stats_.data_inbound_stats.loss_rate);

    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::total[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.total_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)props->net_traffic_stats_.total_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(props->net_traffic_stats_.total_inbound_stats.loss_rate);

    ImGui::TableNextColumn();
    ImGui::Text("FPS");
    ImGui::TableNextColumn();
    ImGui::Text("%d", props->fps_);
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();

    ImGui::EndTable();
  }
  ImGui::SetWindowFontScale(1.0f);

  return 0;
}
}  // namespace crossdesk