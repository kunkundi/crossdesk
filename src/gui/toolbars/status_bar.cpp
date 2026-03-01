#include "layout_relative.h"
#include "localization.h"
#include "render.h"

namespace crossdesk {

int Render::StatusBar() {
  ImGuiIO& io = ImGui::GetIO();
  float status_bar_width = io.DisplaySize.x;
  float status_bar_height = io.DisplaySize.y * STATUS_BAR_HEIGHT;

  static bool a, b, c, d, e;
  ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y * (1 - STATUS_BAR_HEIGHT)),
                          ImGuiCond_Always);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
  ImGui::BeginChild("StatusBar", ImVec2(status_bar_width, status_bar_height),
                    ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor(2);

  ImVec2 dot_pos = ImVec2(status_bar_width * 0.025f,
                          io.DisplaySize.y * (1 - STATUS_BAR_HEIGHT * 0.5f));
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddCircleFilled(dot_pos, status_bar_height * 0.25f,
                             ImColor(1.0f, 1.0f, 1.0f), 100);
  draw_list->AddCircleFilled(dot_pos, status_bar_height * 0.2f,
                             ImColor(signal_connected_ ? 0.0f : 1.0f,
                                     signal_connected_ ? 1.0f : 0.0f, 0.0f),
                             100);

  ImGui::SetWindowFontScale(0.6f);
  draw_list->AddText(
      ImVec2(status_bar_width * 0.045f,
             io.DisplaySize.y * (1 - STATUS_BAR_HEIGHT * 0.9f)),
      ImColor(0.0f, 0.0f, 0.0f),
      signal_connected_
          ? localization::signal_connected[localization_language_index_].c_str()
          : localization::signal_disconnected[localization_language_index_]
                .c_str());
  ImGui::SetWindowFontScale(1.0f);

  ImGui::EndChild();
  return 0;
}
}  // namespace crossdesk