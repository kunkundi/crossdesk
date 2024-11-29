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

int Render::ControlBar() {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImVec2 mouse_button_pos = ImVec2(0, 0);
  if (control_bar_expand_) {
    ImGui::SetCursorPosX(
        is_control_bar_in_left_ ? (control_window_width_ + 5.0f) : 38.0f);
    // mouse control button
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (is_control_bar_in_left_) {
      draw_list->AddLine(
          ImVec2(ImGui::GetCursorScreenPos().x - 5.0f,
                 ImGui::GetCursorScreenPos().y - 7.0f),
          ImVec2(ImGui::GetCursorScreenPos().x - 5.0f,
                 ImGui::GetCursorScreenPos().y - 7.0f + control_window_height_),
          IM_COL32(178, 178, 178, 255), 1.0f);
    }

    mouse_button_pos = ImGui::GetCursorScreenPos();
    float disable_mouse_x = ImGui::GetCursorScreenPos().x + 4.0f;
    float disable_mouse_y = ImGui::GetCursorScreenPos().y + 4.0f;
    std::string mouse = mouse_control_button_pressed_ ? ICON_FA_COMPUTER_MOUSE
                                                      : ICON_FA_COMPUTER_MOUSE;
    if (ImGui::Button(mouse.c_str(), ImVec2(25, 25))) {
      if (connection_established_) {
        control_mouse_ = !control_mouse_;
        start_keyboard_capturer_ = !start_keyboard_capturer_;
        mouse_control_button_pressed_ = !mouse_control_button_pressed_;
        mouse_control_button_label_ =
            mouse_control_button_pressed_
                ? localization::release_mouse[localization_language_index_]
                : localization::control_mouse[localization_language_index_];
      }
    }
    if (!mouse_control_button_pressed_) {
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
    std::string audio = audio_capture_button_pressed_ ? ICON_FA_VOLUME_HIGH
                                                      : ICON_FA_VOLUME_HIGH;
    if (ImGui::Button(audio.c_str(), ImVec2(25, 25))) {
      if (connection_established_) {
        audio_capture_ = !audio_capture_;
        audio_capture_button_pressed_ = !audio_capture_button_pressed_;
        audio_capture_button_label_ =
            audio_capture_button_pressed_
                ? localization::audio_capture[localization_language_index_]
                : localization::mute[localization_language_index_];

        RemoteAction remote_action;
        remote_action.type = ControlType::audio_capture;
        remote_action.a = audio_capture_button_pressed_;
        SendDataFrame(peer_, (const char*)&remote_action,
                      sizeof(remote_action));
      }
    }
    if (!audio_capture_button_pressed_) {
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
    if (net_traffic_stats_button_pressed_) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(66 / 255.0f, 150 / 255.0f,
                                                    250 / 255.0f, 1.0f));
      button_color_style_pushed = true;
    }
    std::string net_traffic_stats = ICON_FA_SIGNAL;
    if (ImGui::Button(net_traffic_stats.c_str(), ImVec2(25, 25))) {
      net_traffic_stats_button_pressed_ = !net_traffic_stats_button_pressed_;
      control_window_height_is_changing_ = true;
      net_traffic_stats_button_pressed_time_ = ImGui::GetTime();
      net_traffic_stats_button_label_ =
          net_traffic_stats_button_pressed_
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
      fullscreen_button_label_ =
          fullscreen_button_pressed_
              ? localization::exit_fullscreen[localization_language_index_]
              : localization::fullscreen[localization_language_index_];
      // save stream window last size
      SDL_GetWindowSize(stream_window_, &stream_window_width_last_,
                        &stream_window_height_last_);
      if (fullscreen_button_pressed_) {
        SDL_SetWindowFullscreen(stream_window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
      } else {
        SDL_SetWindowFullscreen(stream_window_, SDL_FALSE);
      }
      reset_control_bar_pos_ = true;
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

    if (!is_control_bar_in_left_) {
      draw_list->AddLine(
          ImVec2(ImGui::GetCursorScreenPos().x - 3.0f,
                 ImGui::GetCursorScreenPos().y - 7.0f),
          ImVec2(ImGui::GetCursorScreenPos().x - 3.0f,
                 ImGui::GetCursorScreenPos().y - 7.0f + control_window_height_),
          IM_COL32(178, 178, 178, 255), 1.0f);
    }
  }

  ImGui::SetCursorPosX(
      is_control_bar_in_left_ ? (control_window_width_ * 2 - 20.0f) : 5.0f);

  std::string control_bar =
      control_bar_expand_
          ? (is_control_bar_in_left_ ? ICON_FA_ANGLE_LEFT : ICON_FA_ANGLE_RIGHT)
          : (is_control_bar_in_left_ ? ICON_FA_ANGLE_RIGHT
                                     : ICON_FA_ANGLE_LEFT);
  if (ImGui::Button(control_bar.c_str(), ImVec2(15, 25))) {
    control_bar_expand_ = !control_bar_expand_;
    control_bar_button_pressed_time_ = ImGui::GetTime();
    control_window_width_is_changing_ = true;

    if (!control_bar_expand_) {
      control_window_height_ = 40;
      net_traffic_stats_button_pressed_ = false;
    }
  }

  if (net_traffic_stats_button_pressed_ && control_bar_expand_) {
    NetTrafficStats();
  }

  ImGui::PopStyleVar();

  return 0;
}

int Render::NetTrafficStats() {
  ImGui::SetCursorPos(ImVec2(
      is_control_bar_in_left_ ? (control_window_width_ + 5.0f) : 5.0f, 40.0f));

  if (ImGui::BeginTable("split", 3, ImGuiTableFlags_BordersH,
                        ImVec2(control_window_max_width_ - 10.0f,
                               control_window_max_height_ - 40.0f))) {
    ImGui::TableNextColumn();
    ImGui::Text(" ");
    ImGui::TableNextColumn();
    ImGui::Text("%s", localization::in[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s", localization::out[localization_language_index_].c_str());

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::video[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.video_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.video_outbound_stats.bitrate);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::audio[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.audio_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.audio_outbound_stats.bitrate);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s", localization::data[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.data_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.data_outbound_stats.bitrate);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s",
                localization::total[localization_language_index_].c_str());
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.total_inbound_stats.bitrate);
    ImGui::TableNextColumn();
    BitrateDisplay((int)net_traffic_stats_.total_outbound_stats.bitrate);

    ImGui::EndTable();
  }

  return 0;
}
