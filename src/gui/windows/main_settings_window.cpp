#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"
#include "tinyfiledialogs.h"

namespace crossdesk {

int Render::SettingWindow() {
  ImGuiIO& io = ImGui::GetIO();
  if (show_settings_window_) {
    if (settings_window_pos_reset_) {
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
#if (((defined(_WIN32) || defined(__linux__)) && !defined(__aarch64__) && \
      !defined(__arm__) && USE_CUDA) ||                                   \
     defined(__APPLE__))
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.343f, io.DisplaySize.y * 0.05f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.315f, io.DisplaySize.y * 0.9f));
#else
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.343f, io.DisplaySize.y * 0.08f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.315f, io.DisplaySize.y * 0.85f));
#endif
      } else {
#if (((defined(_WIN32) || defined(__linux__)) && !defined(__aarch64__) && \
      !defined(__arm__) && USE_CUDA) ||                                   \
     defined(__APPLE__))
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.297f, io.DisplaySize.y * 0.05f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.407f, io.DisplaySize.y * 0.9f));
#else
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.297f, io.DisplaySize.y * 0.08f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.407f, io.DisplaySize.y * 0.85f));
#endif
      }

      settings_window_pos_reset_ = false;
    }

    // Settings
    {
      static int settings_items_padding = title_bar_button_width_ * 0.75f;
      int settings_items_offset = 0;

      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);

      ImGui::Begin(localization::settings[localization_language_index_].c_str(),
                   nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings);
      ImGui::SetWindowFontScale(0.5f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      {
        const char* language_items[] = {
            localization::language_zh[localization_language_index_].c_str(),
            localization::language_en[localization_language_index_].c_str()};

        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s", localization::language[localization_language_index_].c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 3.0f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.5f);
        }

        ImGui::SetNextItemWidth(title_bar_button_width_ * 1.8f);
        if (ImGui::BeginCombo("##language",
                              language_items[language_button_value_])) {
          ImGui::SetWindowFontScale(0.5f);
          for (int i = 0; i < IM_ARRAYSIZE(language_items); i++) {
            bool selected = (i == language_button_value_);
            if (ImGui::Selectable(language_items[i], selected))
              language_button_value_ = i;
          }

          ImGui::EndCombo();
        }
      }

      ImGui::Separator();

      if (stream_window_inited_) {
        ImGui::BeginDisabled();
      }

      {
        const char* video_quality_items[] = {
            localization::video_quality_low[localization_language_index_]
                .c_str(),
            localization::video_quality_medium[localization_language_index_]
                .c_str(),
            localization::video_quality_high[localization_language_index_]
                .c_str()};

        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s",
            localization::video_quality[localization_language_index_].c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 3.0f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.5f);
        }

        ImGui::SetNextItemWidth(title_bar_button_width_ * 1.8f);
        if (ImGui::BeginCombo(
                "##video_quality",
                video_quality_items[video_quality_button_value_])) {
          ImGui::SetWindowFontScale(0.5f);
          for (int i = 0; i < IM_ARRAYSIZE(video_quality_items); i++) {
            bool selected = (i == video_quality_button_value_);
            if (ImGui::Selectable(video_quality_items[i], selected))
              video_quality_button_value_ = i;
          }

          ImGui::EndCombo();
        }
      }

      ImGui::Separator();

      {
        const char* video_frame_rate_items[] = {"30 fps", "60 fps"};

        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s",
                    localization::video_frame_rate[localization_language_index_]
                        .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 3.0f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.5f);
        }

        ImGui::SetNextItemWidth(title_bar_button_width_ * 1.8f);
        if (ImGui::BeginCombo(
                "##video_frame_rate",
                video_frame_rate_items[video_frame_rate_button_value_])) {
          ImGui::SetWindowFontScale(0.5f);
          for (int i = 0; i < IM_ARRAYSIZE(video_frame_rate_items); i++) {
            bool selected = (i == video_frame_rate_button_value_);
            if (ImGui::Selectable(video_frame_rate_items[i], selected))
              video_frame_rate_button_value_ = i;
          }

          ImGui::EndCombo();
        }
      }

      ImGui::Separator();

      {
        const char* video_encode_format_items[] = {
            localization::h264[localization_language_index_].c_str(),
            localization::av1[localization_language_index_].c_str()};

        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s",
            localization::video_encode_format[localization_language_index_]
                .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 3.0f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.5f);
        }

        ImGui::SetNextItemWidth(title_bar_button_width_ * 1.8f);
        if (ImGui::BeginCombo(
                "##video_encode_format",
                video_encode_format_items[video_encode_format_button_value_])) {
          ImGui::SetWindowFontScale(0.5f);
          for (int i = 0; i < IM_ARRAYSIZE(video_encode_format_items); i++) {
            bool selected = (i == video_encode_format_button_value_);
            if (ImGui::Selectable(video_encode_format_items[i], selected))
              video_encode_format_button_value_ = i;
          }

          ImGui::EndCombo();
        }
      }

#if (((defined(_WIN32) || defined(__linux__)) && !defined(__aarch64__) && \
      !defined(__arm__) && USE_CUDA) ||                                   \
     defined(__APPLE__))
      ImGui::Separator();

      {
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", localization::enable_hardware_video_codec
                              [localization_language_index_]
                                  .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.275f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 5.755f);
        }

        ImGui::Checkbox("##enable_hardware_video_codec",
                        &enable_hardware_video_codec_);
      }
#endif

      ImGui::Separator();

      {
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s",
            localization::enable_turn[localization_language_index_].c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.275f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 5.755f);
        }

        ImGui::Checkbox("##enable_turn", &enable_turn_);
      }

      ImGui::Separator();

      {
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s",
            localization::enable_srtp[localization_language_index_].c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.275f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 5.755f);
        }

        ImGui::Checkbox("##enable_srtp", &enable_srtp_);
      }

      ImGui::Separator();

      {
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset + 1);
        ImGui::AlignTextToFramePadding();
        if (ImGui::Button(localization::self_hosted_server_config
                              [localization_language_index_]
                                  .c_str())) {
          show_self_hosted_server_config_window_ = true;
        }
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.275f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 5.755f);
        }

        ImGui::Checkbox("##enable_self_hosted", &enable_self_hosted_);
      }

      ImGui::Separator();

      {
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s",
                    localization::enable_autostart[localization_language_index_]
                        .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.275f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 5.755f);
        }

        ImGui::Checkbox("##enable_autostart_", &enable_autostart_);
      }

      ImGui::Separator();

      {
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s",
            localization::enable_daemon[localization_language_index_].c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.275f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 5.755f);
        }

        ImGui::Checkbox("##enable_daemon_", &enable_daemon_);
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::SetWindowFontScale(0.5f);
          ImGui::Text("%s", localization::takes_effect_after_restart
                                [localization_language_index_]
                                    .c_str());
          ImGui::SetWindowFontScale(1.0f);
          ImGui::EndTooltip();
        }
      }

      ImGui::Separator();

      {
#ifndef _WIN32
        ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
#endif
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s",
                    localization::minimize_to_tray[localization_language_index_]
                        .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.275f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 5.755f);
        }

        ImGui::Checkbox("##enable_minimize_to_tray_",
                        &enable_minimize_to_tray_);
#ifndef _WIN32
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
#endif
      }

      ImGui::Separator();

      {
        settings_items_offset += settings_items_padding;
        ImGui::SetCursorPosY(settings_items_offset);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(
            "%s",
            localization::file_transfer_save_path[localization_language_index_]
                .c_str());
        ImGui::SameLine();
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(title_bar_button_width_ * 2.82f);
        } else {
          ImGui::SetCursorPosX(title_bar_button_width_ * 4.3f);
        }

        std::string display_path =
            strlen(file_transfer_save_path_buf_) > 0
                ? file_transfer_save_path_buf_
                : localization::default_desktop[localization_language_index_];
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushFont(main_windows_system_chinese_font_);
        if (ImGui::Button(display_path.c_str(),
                          ImVec2(title_bar_button_width_ * 2.0f, 0))) {
          const char* folder =
              tinyfd_selectFolderDialog(localization::file_transfer_save_path
                                            [localization_language_index_]
                                                .c_str(),
                                        strlen(file_transfer_save_path_buf_) > 0
                                            ? file_transfer_save_path_buf_
                                            : nullptr);
          if (folder) {
            strncpy(file_transfer_save_path_buf_, folder,
                    sizeof(file_transfer_save_path_buf_) - 1);
            file_transfer_save_path_buf_[sizeof(file_transfer_save_path_buf_) -
                                         1] = '\0';
          }
        }
        if (ImGui::IsItemHovered() &&
            strlen(file_transfer_save_path_buf_) > 0) {
          ImGui::BeginTooltip();
          ImGui::SetWindowFontScale(0.5f);
          ImGui::Text("%s", file_transfer_save_path_buf_);
          ImGui::SetWindowFontScale(1.0f);
          ImGui::EndTooltip();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);
      }

      if (stream_window_inited_) {
        ImGui::EndDisabled();
      }

      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetCursorPosX(title_bar_button_width_ * 1.59f);
      } else {
        ImGui::SetCursorPosX(title_bar_button_width_ * 2.22f);
      }

      settings_items_offset +=
          settings_items_padding + title_bar_button_width_ * 0.3f;
      ImGui::SetCursorPosY(settings_items_offset);

      ImGui::PopStyleVar();

      // OK
      if (ImGui::Button(
              localization::ok[localization_language_index_].c_str())) {
        show_settings_window_ = false;
        show_self_hosted_server_config_window_ = false;

        // Language
        if (language_button_value_ == 0) {
          config_center_->SetLanguage(ConfigCenter::LANGUAGE::CHINESE);
        } else {
          config_center_->SetLanguage(ConfigCenter::LANGUAGE::ENGLISH);
        }
        language_button_value_last_ = language_button_value_;
        localization_language_ = (ConfigCenter::LANGUAGE)language_button_value_;
        localization_language_index_ = language_button_value_;
        LOG_INFO("Set localization language: {}",
                 localization_language_index_ == 0 ? "zh" : "en");

        // Video quality
        if (video_quality_button_value_ == 0) {
          config_center_->SetVideoQuality(ConfigCenter::VIDEO_QUALITY::LOW);
        } else if (video_quality_button_value_ == 1) {
          config_center_->SetVideoQuality(ConfigCenter::VIDEO_QUALITY::MEDIUM);
        } else {
          config_center_->SetVideoQuality(ConfigCenter::VIDEO_QUALITY::HIGH);
        }
        video_quality_button_value_last_ = video_quality_button_value_;

        if (video_frame_rate_button_value_ == 0) {
          config_center_->SetVideoFrameRate(
              ConfigCenter::VIDEO_FRAME_RATE::FPS_30);
        } else if (video_frame_rate_button_value_ == 1) {
          config_center_->SetVideoFrameRate(
              ConfigCenter::VIDEO_FRAME_RATE::FPS_60);
        }
        video_frame_rate_button_value_last_ = video_frame_rate_button_value_;

        // Video encode format
        if (video_encode_format_button_value_ == 0) {
          config_center_->SetVideoEncodeFormat(
              ConfigCenter::VIDEO_ENCODE_FORMAT::H264);
        } else if (video_encode_format_button_value_ == 1) {
          config_center_->SetVideoEncodeFormat(
              ConfigCenter::VIDEO_ENCODE_FORMAT::AV1);
        }
        video_encode_format_button_value_last_ =
            video_encode_format_button_value_;

        // Hardware video codec
        if (enable_hardware_video_codec_) {
          config_center_->SetHardwareVideoCodec(true);
        } else {
          config_center_->SetHardwareVideoCodec(false);
        }
        enable_hardware_video_codec_last_ = enable_hardware_video_codec_;

        // TURN mode
        if (enable_turn_) {
          config_center_->SetTurn(true);
        } else {
          config_center_->SetTurn(false);
        }
        enable_turn_last_ = enable_turn_;

        // SRTP
        if (enable_srtp_) {
          config_center_->SetSrtp(true);
        } else {
          config_center_->SetSrtp(false);
        }
        enable_srtp_last_ = enable_srtp_;

        if (enable_self_hosted_) {
          config_center_->SetSelfHosted(true);
        } else {
          config_center_->SetSelfHosted(false);
        }
        enable_self_hosted_last_ = enable_self_hosted_;

        if (enable_autostart_) {
          config_center_->SetAutostart(true);
        } else {
          config_center_->SetAutostart(false);
        }
        enable_autostart_last_ = enable_autostart_;

        if (enable_daemon_) {
          config_center_->SetDaemon(true);
        } else {
          config_center_->SetDaemon(false);
        }
        enable_daemon_last_ = enable_daemon_;

#if _WIN32
        if (enable_minimize_to_tray_) {
          config_center_->SetMinimizeToTray(true);
        } else {
          config_center_->SetMinimizeToTray(false);
        }
        enable_minimize_to_tray_last_ = enable_minimize_to_tray_;
#endif

        // File transfer save path
        config_center_->SetFileTransferSavePath(file_transfer_save_path_buf_);
        file_transfer_save_path_last_ = file_transfer_save_path_buf_;

        settings_window_pos_reset_ = true;

        // Recreate peer instance
        LoadSettingsFromCacheFile();

        // Recreate peer instance
        if (!stream_window_inited_) {
          LOG_INFO("Recreate peer instance");
          CleanupPeers();
          CreateConnectionPeer();
        }
      }

      ImGui::SameLine();
      // Cancel
      if (ImGui::Button(
              localization::cancel[localization_language_index_].c_str())) {
        show_settings_window_ = false;
        show_self_hosted_server_config_window_ = false;

        if (language_button_value_ != language_button_value_last_) {
          language_button_value_ = language_button_value_last_;
        }

        if (video_quality_button_value_ != video_quality_button_value_last_) {
          video_quality_button_value_ = video_quality_button_value_last_;
        }

        if (video_frame_rate_button_value_ !=
            video_frame_rate_button_value_last_) {
          video_frame_rate_button_value_ = video_frame_rate_button_value_last_;
        }

        if (video_encode_format_button_value_ !=
            video_encode_format_button_value_last_) {
          video_encode_format_button_value_ =
              video_encode_format_button_value_last_;
        }

        if (enable_hardware_video_codec_ != enable_hardware_video_codec_last_) {
          enable_hardware_video_codec_ = enable_hardware_video_codec_last_;
        }

        if (enable_turn_ != enable_turn_last_) {
          enable_turn_ = enable_turn_last_;
        }

        // Restore file transfer save path
        strncpy(file_transfer_save_path_buf_,
                file_transfer_save_path_last_.c_str(),
                sizeof(file_transfer_save_path_buf_) - 1);
        file_transfer_save_path_buf_[sizeof(file_transfer_save_path_buf_) - 1] =
            '\0';

        settings_window_pos_reset_ = true;
      }
      ImGui::SetWindowFontScale(0.5f);
      ImGui::End();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();
    }
  }

  return 0;
}
}  // namespace crossdesk