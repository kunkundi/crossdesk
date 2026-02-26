#include "rounded_corner_button.h"

namespace crossdesk {
bool RoundedCornerButton(const char* label, const ImVec2& size, float rounding,
                         ImDrawFlags round_flags, bool enabled,
                         ImU32 normal_col, ImU32 hover_col, ImU32 active_col,
                         ImU32 border_col) {
  ImGuiWindow* current_window = ImGui::GetCurrentWindow();
  if (current_window->SkipItems) return false;

  const ImGuiStyle& style = ImGui::GetStyle();

  ImGuiID button_id = current_window->GetID(label);
  ImVec2 cursor_pos = current_window->DC.CursorPos;
  ImVec2 button_size = ImGui::CalcItemSize(size, 0.0f, 0.0f);
  ImRect button_rect(cursor_pos, ImVec2(cursor_pos.x + button_size.x,
                                        cursor_pos.y + button_size.y));
  ImGui::ItemSize(button_rect);
  if (!ImGui::ItemAdd(button_rect, button_id)) return false;

  bool is_hovered = false, is_held = false;
  bool is_pressed = false;
  if (enabled) {
    is_pressed =
        ImGui::ButtonBehavior(button_rect, button_id, &is_hovered, &is_held);
  }

  if (normal_col == 0) normal_col = ImGui::GetColorU32(ImGuiCol_Button);
  if (hover_col == 0) hover_col = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  if (active_col == 0) active_col = ImGui::GetColorU32(ImGuiCol_ButtonActive);
  if (border_col == 0) border_col = ImGui::GetColorU32(ImGuiCol_Border);

  ImU32 fill_color = normal_col;
  if (is_held && is_hovered)
    fill_color = active_col;
  else if (is_hovered)
    fill_color = hover_col;

  if (!enabled) fill_color = IM_COL32(120, 120, 120, 180);

  ImDrawList* window_draw_list = ImGui::GetWindowDrawList();

  window_draw_list->AddRectFilled(button_rect.Min, button_rect.Max, fill_color,
                                  rounding, round_flags);

  if (style.FrameBorderSize > 0.0f) {
    window_draw_list->AddRect(button_rect.Min, button_rect.Max, border_col,
                              rounding, round_flags, style.FrameBorderSize);
  }

  ImU32 text_color =
      ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);

  const char* label_end = ImGui::FindRenderedTextEnd(label);
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::ColorConvertU32ToFloat4(text_color));
  ImGui::RenderTextClipped(button_rect.Min, button_rect.Max, label, label_end,
                           nullptr, ImVec2(0.5f, 0.5f), &button_rect);
  ImGui::PopStyleColor();

  return is_pressed;
}
}  // namespace crossdesk
