#include "layout_style.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

int Render::SettingWindow() {
  if (show_settings_window_) {
    if (settings_window_pos_reset_) {
      const ImGuiViewport *viewport = ImGui::GetMainViewport();
      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetNextWindowPos(
            ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                    SETTINGS_WINDOW_WIDTH_CN) /
                       2,
                   (viewport->WorkSize.y - viewport->WorkPos.y -
                    SETTINGS_WINDOW_HEIGHT_CN) /
                       2));

        ImGui::SetNextWindowSize(
            ImVec2(SETTINGS_WINDOW_WIDTH_CN, SETTINGS_WINDOW_HEIGHT_CN));
      } else {
        ImGui::SetNextWindowPos(
            ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                    SETTINGS_WINDOW_WIDTH_EN) /
                       2,
                   (viewport->WorkSize.y - viewport->WorkPos.y -
                    SETTINGS_WINDOW_HEIGHT_EN) /
                       2));

        ImGui::SetNextWindowSize(
            ImVec2(SETTINGS_WINDOW_WIDTH_EN, SETTINGS_WINDOW_HEIGHT_EN));
      }

      settings_window_pos_reset_ = false;
    }

    // Settings
    {
      ImGui::SetWindowFontScale(0.5f);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

      ImGui::Begin(localization::settings[localization_language_index_].c_str(),
                   nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetWindowFontScale(0.5f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      {
        const char *language_items[] = {
            localization::language_zh[localization_language_index_].c_str(),
            localization::language_en[localization_language_index_].c_str()};

        ImGui::SetCursorPosY(32);
        ImGui::Text(
            "%s", localization::language[localization_language_index_].c_str());
        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(LANGUAGE_SELECT_WINDOW_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(LANGUAGE_SELECT_WINDOW_PADDING_EN);
        }
        ImGui::SetCursorPosY(30);
        ImGui::SetNextItemWidth(SETTINGS_SELECT_WINDOW_WIDTH);

        ImGui::Combo("##language", &language_button_value_, language_items,
                     IM_ARRAYSIZE(language_items));
      }

      ImGui::Separator();

      if (stream_window_inited_) {
        ImGui::BeginDisabled();
      }

      {
        const char *video_quality_items[] = {
            localization::video_quality_high[localization_language_index_]
                .c_str(),
            localization::video_quality_medium[localization_language_index_]
                .c_str(),
            localization::video_quality_low[localization_language_index_]
                .c_str()};

        ImGui::SetCursorPosY(62);
        ImGui::Text(
            "%s",
            localization::video_quality[localization_language_index_].c_str());

        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(VIDEO_QUALITY_SELECT_WINDOW_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(VIDEO_QUALITY_SELECT_WINDOW_PADDING_EN);
        }
        ImGui::SetCursorPosY(60);
        ImGui::SetNextItemWidth(SETTINGS_SELECT_WINDOW_WIDTH);

        ImGui::Combo("##video_quality", &video_quality_button_value_,
                     video_quality_items, IM_ARRAYSIZE(video_quality_items));
      }

      ImGui::Separator();

      {
        const char *video_encode_format_items[] = {
            localization::av1[localization_language_index_].c_str(),
            localization::h264[localization_language_index_].c_str()};

        ImGui::SetCursorPosY(92);
        ImGui::Text(
            "%s",
            localization::video_encode_format[localization_language_index_]
                .c_str());

        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(VIDEO_ENCODE_FORMAT_SELECT_WINDOW_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(VIDEO_ENCODE_FORMAT_SELECT_WINDOW_PADDING_EN);
        }
        ImGui::SetCursorPosY(90);
        ImGui::SetNextItemWidth(SETTINGS_SELECT_WINDOW_WIDTH);

        ImGui::Combo(
            "##video_encode_format", &video_encode_format_button_value_,
            video_encode_format_items, IM_ARRAYSIZE(video_encode_format_items));
      }

      ImGui::Separator();

      {
        ImGui::SetCursorPosY(122);
        ImGui::Text("%s", localization::enable_hardware_video_codec
                              [localization_language_index_]
                                  .c_str());

        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(ENABLE_HARDWARE_VIDEO_CODEC_CHECKBOX_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(ENABLE_HARDWARE_VIDEO_CODEC_CHECKBOX_PADDING_EN);
        }
        ImGui::SetCursorPosY(120);
        ImGui::Checkbox("##enable_hardware_video_codec",
                        &enable_hardware_video_codec_);
      }

      ImGui::Separator();

      {
        ImGui::SetCursorPosY(152);
        ImGui::Text(
            "%s",
            localization::enable_turn[localization_language_index_].c_str());

        if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
          ImGui::SetCursorPosX(ENABLE_TURN_CHECKBOX_PADDING_CN);
        } else {
          ImGui::SetCursorPosX(ENABLE_TURN_CHECKBOX_PADDING_EN);
        }
        ImGui::SetCursorPosY(150);
        ImGui::Checkbox("##enable_turn", &enable_turn_);
      }

      if (stream_window_inited_) {
        ImGui::EndDisabled();
      }

      if (ConfigCenter::LANGUAGE::CHINESE == localization_language_) {
        ImGui::SetCursorPosX(SETTINGS_OK_BUTTON_PADDING_CN);
      } else {
        ImGui::SetCursorPosX(SETTINGS_OK_BUTTON_PADDING_EN);
      }
      ImGui::SetCursorPosY(190.0f);
      ImGui::PopStyleVar();

      // OK
      if (ImGui::Button(
              localization::ok[localization_language_index_].c_str())) {
        show_settings_window_ = false;

        // Language
        if (language_button_value_ == 0) {
          config_center_.SetLanguage(ConfigCenter::LANGUAGE::CHINESE);
        } else {
          config_center_.SetLanguage(ConfigCenter::LANGUAGE::ENGLISH);
        }
        language_button_value_last_ = language_button_value_;
        localization_language_ = (ConfigCenter::LANGUAGE)language_button_value_;
        localization_language_index_ = language_button_value_;
        LOG_INFO("Set localization language: {}",
                 localization_language_index_ == 0 ? "zh" : "en");

        // Video quality
        if (video_quality_button_value_ == 0) {
          config_center_.SetVideoQuality(ConfigCenter::VIDEO_QUALITY::HIGH);
        } else if (video_quality_button_value_ == 1) {
          config_center_.SetVideoQuality(ConfigCenter::VIDEO_QUALITY::MEDIUM);
        } else {
          config_center_.SetVideoQuality(ConfigCenter::VIDEO_QUALITY::LOW);
        }
        video_quality_button_value_last_ = video_quality_button_value_;

        // Video encode format
        if (video_encode_format_button_value_ == 0) {
          config_center_.SetVideoEncodeFormat(
              ConfigCenter::VIDEO_ENCODE_FORMAT::AV1);
        } else if (video_encode_format_button_value_ == 1) {
          config_center_.SetVideoEncodeFormat(
              ConfigCenter::VIDEO_ENCODE_FORMAT::H264);
        }
        video_encode_format_button_value_last_ =
            video_encode_format_button_value_;

        // Hardware video codec
        if (enable_hardware_video_codec_) {
          config_center_.SetHardwareVideoCodec(true);
        } else {
          config_center_.SetHardwareVideoCodec(false);
        }
        enable_hardware_video_codec_last_ = enable_hardware_video_codec_;

        // TURN mode
        if (enable_turn_) {
          config_center_.SetTurn(true);
        } else {
          config_center_.SetTurn(false);
        }
        enable_turn_last_ = enable_turn_;

        SaveSettingsIntoCacheFile();
        settings_window_pos_reset_ = true;

        // Recreate peer instance
        LoadSettingsFromCacheFile();

        // Recreate peer instance
        if (!stream_window_inited_) {
          LOG_INFO("Recreate peer instance");
          DestroyPeer(&peer_);
          is_create_connection_ = false;
          CreateConnectionPeer();
        }
      }

      ImGui::SameLine();
      // Cancel
      if (ImGui::Button(
              localization::cancel[localization_language_index_].c_str())) {
        show_settings_window_ = false;
        if (language_button_value_ != language_button_value_last_) {
          language_button_value_ = language_button_value_last_;
        }

        if (video_quality_button_value_ != video_quality_button_value_last_) {
          video_quality_button_value_ = video_quality_button_value_last_;
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

        settings_window_pos_reset_ = true;
      }
      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetWindowFontScale(0.5f);
      ImGui::End();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();
      ImGui::SetWindowFontScale(1.0f);
    }
  }

  return 0;
}