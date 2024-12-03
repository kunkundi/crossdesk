#include "layout_style.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

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

int Render::ControlBar(std::shared_ptr<SubStreamWindowProperties>& properties) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImVec2 mouse_button_pos = ImVec2(0, 0);
  if (properties->control_bar_expand_) {
    ImGui::SetCursorPosX(properties->is_control_bar_in_left_
                             ? (properties->control_window_width_ + 5.0f)
                             : 38.0f);
    // mouse control button
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (properties->is_control_bar_in_left_) {
      draw_list->AddLine(ImVec2(ImGui::GetCursorScreenPos().x - 5.0f,
                                ImGui::GetCursorScreenPos().y - 7.0f),
                         ImVec2(ImGui::GetCursorScreenPos().x - 5.0f,
                                ImGui::GetCursorScreenPos().y - 7.0f +
                                    properties->control_window_height_),
                         IM_COL32(178, 178, 178, 255), 1.0f);
    }

    mouse_button_pos = ImGui::GetCursorScreenPos();
    float disable_mouse_x = ImGui::GetCursorScreenPos().x + 4.0f;
    float disable_mouse_y = ImGui::GetCursorScreenPos().y + 4.0f;
    std::string mouse = properties->mouse_control_button_pressed_
                            ? ICON_FA_COMPUTER_MOUSE
                            : ICON_FA_COMPUTER_MOUSE;
    if (ImGui::Button(mouse.c_str(), ImVec2(25, 25))) {
      if (properties->connection_established_) {
        control_mouse_ = !control_mouse_;
        start_keyboard_capturer_ = !start_keyboard_capturer_;
        properties->mouse_control_button_pressed_ =
            !properties->mouse_control_button_pressed_;
        properties->mouse_control_button_label_ =
            properties->mouse_control_button_pressed_
                ? localization::release_mouse[localization_language_index_]
                : localization::control_mouse[localization_language_index_];
      }
    }
    if (!properties->mouse_control_button_pressed_) {
      draw_list->AddLine(
          ImVec2(disable_mouse_x, disable_mouse_y),
          ImVec2(disable_mouse_x + 16.0f, disable_mouse_y + 14.2f),
          IM_COL32(0, 0, 0, 255), 2.0f);
      draw_list->AddLine(
          ImVec2(disable_mouse_x - 1.2f, disable_mouse_y + 1.2f),
          ImVec2(disable_mouse_x + 15.3f, disable_mouse_y + 15.4f),
          ImGui::IsItemHovered() ? IM_COL32(66, 150, 250, 255)
                                 : IM_COL32(179, 213, 253, 255),
          2.0f);
    }

    ImGui::SameLine();
    // audio capture button
    float disable_audio_x = ImGui::GetCursorScreenPos().x + 4;
    float disable_audio_y = ImGui::GetCursorScreenPos().y + 4.0f;
    // std::string audio = audio_capture_button_pressed_ ? ICON_FA_VOLUME_HIGH
    //                                                   : ICON_FA_VOLUME_XMARK;
    std::string audio = properties->audio_capture_button_pressed_
                            ? ICON_FA_VOLUME_HIGH
                            : ICON_FA_VOLUME_HIGH;
    if (ImGui::Button(audio.c_str(), ImVec2(25, 25))) {
      if (properties->connection_established_) {
        properties->audio_capture_button_pressed_ =
            !properties->audio_capture_button_pressed_;
        properties->audio_capture_button_label_ =
            properties->audio_capture_button_pressed_
                ? localization::audio_capture[localization_language_index_]
                : localization::mute[localization_language_index_];

        RemoteAction remote_action;
        remote_action.type = ControlType::audio_capture;
        remote_action.a = properties->audio_capture_button_pressed_;
        SendDataFrame(peer_, (const char*)&remote_action,
                      sizeof(remote_action));
      }
    }
    if (!properties->audio_capture_button_pressed_) {
      draw_list->AddLine(
          ImVec2(disable_audio_x, disable_audio_y),
          ImVec2(disable_audio_x + 16.0f, disable_audio_y + 14.2f),
          IM_COL32(0, 0, 0, 255), 2.0f);
      draw_list->AddLine(
          ImVec2(disable_audio_x - 1.2f, disable_audio_y + 1.2f),
          ImVec2(disable_audio_x + 15.3f, disable_audio_y + 15.4f),
          ImGui::IsItemHovered() ? IM_COL32(66, 150, 250, 255)
                                 : IM_COL32(179, 213, 253, 255),
          2.0f);
    }

    ImGui::SameLine();
    // net traffic stats button
    bool button_color_style_pushed = false;
    if (properties->net_traffic_stats_button_pressed_) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(66 / 255.0f, 150 / 255.0f,
                                                    250 / 255.0f, 1.0f));
      button_color_style_pushed = true;
    }
    std::string net_traffic_stats = ICON_FA_SIGNAL;
    if (ImGui::Button(net_traffic_stats.c_str(), ImVec2(25, 25))) {
      properties->net_traffic_stats_button_pressed_ =
          !properties->net_traffic_stats_button_pressed_;
      properties->control_window_height_is_changing_ = true;
      properties->net_traffic_stats_button_pressed_time_ = ImGui::GetTime();
      properties->net_traffic_stats_button_label_ =
          properties->net_traffic_stats_button_pressed_
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
    if (ImGui::Button(fullscreen.c_str(), ImVec2(25, 25))) {
      fullscreen_button_pressed_ = !fullscreen_button_pressed_;
      properties->fullscreen_button_label_ =
          fullscreen_button_pressed_
              ? localization::exit_fullscreen[localization_language_index_]
              : localization::fullscreen[localization_language_index_];

      if (fullscreen_button_pressed_) {
        SDL_SetWindowFullscreen(stream_window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
      } else {
        SDL_SetWindowFullscreen(stream_window_, SDL_FALSE);
      }
      properties->reset_control_bar_pos_ = true;
    }

    ImGui::SameLine();
    // close button
    std::string close_button = ICON_FA_XMARK;
    if (ImGui::Button(close_button.c_str(), ImVec2(25, 25))) {
      SDL_Event event;
      event.type = SDL_QUIT;
      SDL_PushEvent(&event);
    }

    ImGui::SameLine();

    if (!properties->is_control_bar_in_left_) {
      draw_list->AddLine(ImVec2(ImGui::GetCursorScreenPos().x - 3.0f,
                                ImGui::GetCursorScreenPos().y - 7.0f),
                         ImVec2(ImGui::GetCursorScreenPos().x - 3.0f,
                                ImGui::GetCursorScreenPos().y - 7.0f +
                                    properties->control_window_height_),
                         IM_COL32(178, 178, 178, 255), 1.0f);
    }
  }

  ImGui::SetCursorPosX(properties->is_control_bar_in_left_
                           ? (properties->control_window_width_ * 2 - 20.0f)
                           : 5.0f);

  std::string control_bar =
      properties->control_bar_expand_
          ? (properties->is_control_bar_in_left_ ? ICON_FA_ANGLE_LEFT
                                                 : ICON_FA_ANGLE_RIGHT)
          : (properties->is_control_bar_in_left_ ? ICON_FA_ANGLE_RIGHT
                                                 : ICON_FA_ANGLE_LEFT);
  if (ImGui::Button(control_bar.c_str(), ImVec2(15, 25))) {
    properties->control_bar_expand_ = !properties->control_bar_expand_;
    properties->control_bar_button_pressed_time_ = ImGui::GetTime();
    properties->control_window_width_is_changing_ = true;

    if (!properties->control_bar_expand_) {
      properties->control_window_height_ = 40;
      properties->net_traffic_stats_button_pressed_ = false;
    }
  }

  if (properties->net_traffic_stats_button_pressed_ &&
      properties->control_bar_expand_) {
    NetTrafficStats(properties);
  }

  ImGui::PopStyleVar();

  return 0;
}

int Render::NetTrafficStats(
    std::shared_ptr<SubStreamWindowProperties>& properties) {
  ImGui::SetCursorPos(ImVec2(properties->is_control_bar_in_left_
                                 ? (properties->control_window_width_ + 5.0f)
                                 : 5.0f,
                             40.0f));

  if (ImGui::BeginTable(
          "NetTrafficStats", 4, ImGuiTableFlags_BordersH,
          ImVec2(properties->control_window_max_width_ - 10.0f,
                 properties->control_window_max_height_ - 40.0f))) {
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

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::video[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.video_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.video_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(
        properties->net_traffic_stats_.video_inbound_stats.loss_rate);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::audio[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.audio_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.audio_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(
        properties->net_traffic_stats_.audio_inbound_stats.loss_rate);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s", localization::data[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.data_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.data_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(
        properties->net_traffic_stats_.data_inbound_stats.loss_rate);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::total[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.total_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay(
        (int)properties->net_traffic_stats_.total_outbound_stats.bitrate);
    ImGui::TableNextColumn();
    LossRateDisplay(
        properties->net_traffic_stats_.total_inbound_stats.loss_rate);

    ImGui::EndTable();
  }

  return 0;
}
