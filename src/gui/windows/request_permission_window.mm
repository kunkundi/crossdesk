#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <unistd.h>
#include <cstdlib>

namespace crossdesk {

bool Render::DrawToggleSwitch(const char* id, bool active, bool enabled) {
  const float TRACK_HEIGHT = ImGui::GetFrameHeight();
  const float TRACK_WIDTH = TRACK_HEIGHT * 1.8f;
  const float TRACK_RADIUS = TRACK_HEIGHT * 0.5f;
  const float KNOB_PADDING = 2.0f;
  const float KNOB_HEIGHT = TRACK_HEIGHT - 4.0f;
  const float KNOB_WIDTH = KNOB_HEIGHT * 1.2f;
  const float KNOB_RADIUS = KNOB_HEIGHT * 0.5f;
  const float DISABLED_ALPHA = 0.6f;
  const float KNOB_ALPHA_DISABLED = 0.9f;

  const ImVec4 COLOR_ACTIVE = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
  const ImVec4 COLOR_ACTIVE_HOVER = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
  const ImVec4 COLOR_INACTIVE = ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
  const ImVec4 COLOR_INACTIVE_HOVER = ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
  const ImVec4 COLOR_KNOB = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 track_pos = ImGui::GetCursorScreenPos();

  ImGui::InvisibleButton(id, ImVec2(TRACK_WIDTH, TRACK_HEIGHT));
  bool hovered = ImGui::IsItemHovered();
  bool clicked = ImGui::IsItemClicked() && enabled;

  ImVec4 track_color = active ? (hovered && enabled ? COLOR_ACTIVE_HOVER : COLOR_ACTIVE)
                              : (hovered && enabled ? COLOR_INACTIVE_HOVER : COLOR_INACTIVE);

  if (!enabled) {
    track_color.w *= DISABLED_ALPHA;
  }

  ImVec2 track_min = ImVec2(track_pos.x, track_pos.y + 0.5f);
  ImVec2 track_max = ImVec2(track_pos.x + TRACK_WIDTH, track_pos.y + TRACK_HEIGHT - 0.5f);
  draw_list->AddRectFilled(track_min, track_max, ImGui::GetColorU32(track_color), TRACK_RADIUS);

  float knob_position = active ? 1.0f : 0.0f;
  float knob_min_x = track_pos.x + KNOB_PADDING;
  float knob_max_x = track_pos.x + TRACK_WIDTH - KNOB_WIDTH - KNOB_PADDING;
  float knob_x = knob_min_x + knob_position * (knob_max_x - knob_min_x);
  float knob_y = track_pos.y + (TRACK_HEIGHT - KNOB_HEIGHT) * 0.5f;

  ImVec4 knob_color = COLOR_KNOB;
  if (!enabled) {
    knob_color.w = KNOB_ALPHA_DISABLED;
  }

  ImVec2 knob_min = ImVec2(knob_x, knob_y);
  ImVec2 knob_max = ImVec2(knob_x + KNOB_WIDTH, knob_y + KNOB_HEIGHT);
  draw_list->AddRectFilled(knob_min, knob_max, ImGui::GetColorU32(knob_color), KNOB_RADIUS);

  return clicked;
}

bool Render::CheckScreenRecordingPermission() {
  // CGPreflightScreenCaptureAccess is available on macOS 10.15+
  if (@available(macOS 10.15, *)) {
    bool granted = CGPreflightScreenCaptureAccess();
    return granted;
  }
  // for older macOS versions, assume permission is granted
  return true;
}

bool Render::CheckAccessibilityPermission() {
  NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt : @NO};
  bool trusted = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
  return trusted;
}

void Render::OpenAccessibilityPreferences() {
  NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt : @YES};
  AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);

  system("open "
         "\"x-apple.systempreferences:com.apple.preference.security?Privacy_"
         "Accessibility\"");
}

void Render::OpenScreenRecordingPreferences() {
  if (@available(macOS 10.15, *)) {
    CGRequestScreenCaptureAccess();
  }

  system("open "
         "\"x-apple.systempreferences:com.apple.preference.security?Privacy_"
         "ScreenCapture\"");
}

int Render::RequestPermissionWindow() {
  bool screen_recording_granted = CheckScreenRecordingPermission();
  bool accessibility_granted = CheckAccessibilityPermission();

  show_request_permission_window_ = !screen_recording_granted || !accessibility_granted;

  if (!show_request_permission_window_) {
    return 0;
  }

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  float window_width = localization_language_index_ == 0 ? REQUEST_PERMISSION_WINDOW_WIDTH_CN
                                                         : REQUEST_PERMISSION_WINDOW_WIDTH_EN;
  float window_height = localization_language_index_ == 0 ? REQUEST_PERMISSION_WINDOW_HEIGHT_CN
                                                          : REQUEST_PERMISSION_WINDOW_HEIGHT_EN;

  float checkbox_padding = localization_language_index_ == 0
                               ? REQUEST_PERMISSION_WINDOW_CHECKBOX_PADDING_CN
                               : REQUEST_PERMISSION_WINDOW_CHECKBOX_PADDING_EN;

  ImVec2 center_pos = ImVec2((viewport->WorkSize.x - window_width) * 0.5f + viewport->WorkPos.x,
                             (viewport->WorkSize.y - window_height) * 0.5f + viewport->WorkPos.y);
  ImGui::SetNextWindowPos(center_pos, ImGuiCond_Once);

  ImGui::SetNextWindowSize(ImVec2(window_width, window_height), ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  ImGui::Begin(
      localization::request_permissions[localization_language_index_].c_str(), nullptr,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

  ImGui::SetWindowFontScale(0.3f);

  // use system font
  if (main_windows_system_chinese_font_ != nullptr) {
    ImGui::PushFont(main_windows_system_chinese_font_);
  }

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() + 5.0f);
  ImGui::SetCursorPosX(10.0f);
  ImGui::TextWrapped(
      "%s", localization::permission_required_message[localization_language_index_].c_str());

  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();

  // accessibility permission
  ImGui::SetCursorPosX(10.0f);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("1. %s:",
              localization::accessibility_permission[localization_language_index_].c_str());
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::SetCursorPosX(checkbox_padding);
  if (accessibility_granted) {
    DrawToggleSwitch("accessibility_toggle_on", true, false);
  } else {
    if (DrawToggleSwitch("accessibility_toggle", accessibility_granted, !accessibility_granted)) {
      OpenAccessibilityPreferences();
    }
  }

  ImGui::Spacing();

  // screen recording permission
  ImGui::SetCursorPosX(10.0f);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("2. %s:",
              localization::screen_recording_permission[localization_language_index_].c_str());
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::SetCursorPosX(checkbox_padding);
  if (screen_recording_granted) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    DrawToggleSwitch("screen_recording_toggle_on", true, false);
  } else {
    if (DrawToggleSwitch("screen_recording_toggle", screen_recording_granted,
                         !screen_recording_granted)) {
      OpenScreenRecordingPreferences();
    }
  }

  ImGui::SetWindowFontScale(1.0f);
  ImGui::SetWindowFontScale(0.45f);

  // pop system font
  if (main_windows_system_chinese_font_ != nullptr) {
    ImGui::PopFont();
  }

  ImGui::End();
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor();

  return 0;
}
}  // namespace crossdesk