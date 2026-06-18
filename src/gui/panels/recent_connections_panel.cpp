#include <algorithm>
#include <cctype>

#include "layout_relative.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

namespace crossdesk {
namespace {

std::string TrimConnectionAlias(const char* value) {
  std::string alias = value ? value : "";

  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  alias.erase(alias.begin(),
              std::find_if(alias.begin(), alias.end(), not_space));
  alias.erase(std::find_if(alias.rbegin(), alias.rend(), not_space).base(),
              alias.end());

  return alias;
}

void SetDarkTextTooltip(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
  ImGui::BeginTooltip();
  ImGui::SetWindowFontScale(0.5f);
  ImGui::Text("%s", text);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::EndTooltip();
  ImGui::PopStyleColor();
}

}  // namespace

int Render::RecentConnectionsWindow() {
  ImGuiIO& io = ImGui::GetIO();
  float recent_connection_window_width = io.DisplaySize.x;
  float recent_connection_window_height =
      io.DisplaySize.y * (0.455f - STATUS_BAR_HEIGHT);
  ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y * 0.55f),
                          ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild(
      "RecentConnectionsWindow",
      ImVec2(recent_connection_window_width, recent_connection_window_height),
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SetCursorPos(
      ImVec2(io.DisplaySize.x * 0.045f, io.DisplaySize.y * 0.02f));

  ImGui::SetWindowFontScale(0.9f);
  ImGui::TextColored(
      ImVec4(0.0f, 0.0f, 0.0f, 0.5f), "%s",
      localization::recent_connections[localization_language_index_].c_str());

  ShowRecentConnections();

  ImGui::EndChild();

  return 0;
}

int Render::ShowRecentConnections() {
  ImGuiIO& io = ImGui::GetIO();
  float recent_connection_panel_width = io.DisplaySize.x * 0.912f;
  float recent_connection_panel_height = io.DisplaySize.y * 0.29f;
  float recent_connection_image_height = recent_connection_panel_height * 0.6f;
  float recent_connection_image_width = recent_connection_image_height * 16 / 9;
  float recent_connection_sub_container_width =
      recent_connection_image_width * 1.2f;
  float recent_connection_sub_container_height =
      recent_connection_image_height * 1.4f;
  float recent_connection_button_width = recent_connection_image_width * 0.15f;
  float recent_connection_button_height =
      recent_connection_image_height * 0.25f;
  float recent_connection_footer_height =
      recent_connection_button_height * 1.18f;
  float recent_connection_name_width = recent_connection_image_width;

  ImGui::SetCursorPos(
      ImVec2(io.DisplaySize.x * 0.045f, io.DisplaySize.y * 0.1f));

  std::map<std::string, ImVec2> sub_containers_pos;
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImVec4(239.0f / 255, 240.0f / 255, 242.0f / 255, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::BeginChild(
      "RecentConnectionsContainer",
      ImVec2(recent_connection_panel_width, recent_connection_panel_height),
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoBringToFrontOnFocus |
          ImGuiWindowFlags_AlwaysHorizontalScrollbar |
          ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  size_t recent_connections_count = recent_connections_.size();
  int count = 0;
  for (auto& it : recent_connections_) {
    sub_containers_pos[it.first] = ImGui::GetCursorPos();
    std::string recent_connection_sub_window_name =
        "RecentConnectionsSubContainer" + it.first;
    // recent connections sub container
    ImGui::BeginChild(recent_connection_sub_window_name.c_str(),
                      ImVec2(recent_connection_sub_container_width,
                             recent_connection_sub_container_height),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoBringToFrontOnFocus);
    std::string connection_info = it.first;

    // remote id length is 9
    // password length is 6
    // connection_info -> remote_id + 'Y' + host_name + '@' + password
    //                 -> remote_id + 'N' + host_name
    bool invalid_connection_info = false;
    if (connection_info.size() > 9 && 'Y' == connection_info[9] &&
        connection_info.size() >= 16) {
      size_t pos_y = connection_info.find('Y');
      size_t pos_at = connection_info.find('@');

      if (pos_y == std::string::npos || pos_at == std::string::npos ||
          pos_y >= pos_at) {
        LOG_ERROR("Invalid filename");
        invalid_connection_info = true;
      } else {
        it.second.remote_id = connection_info.substr(0, pos_y);
        it.second.remote_host_name =
            connection_info.substr(pos_y + 1, pos_at - pos_y - 1);
        it.second.password = connection_info.substr(pos_at + 1);
        it.second.remember_password = true;
      }
    } else if (connection_info.size() > 9 && 'N' == connection_info[9] &&
               connection_info.size() >= 10) {
      size_t pos_n = connection_info.find('N');

      if (pos_n == std::string::npos) {
        LOG_ERROR("Invalid filename");
        invalid_connection_info = true;
      } else {
        it.second.remote_id = connection_info.substr(0, pos_n);
        it.second.remote_host_name = connection_info.substr(pos_n + 1);
        it.second.password = "";
        it.second.remember_password = false;
      }
    } else {
      invalid_connection_info = true;
    }

    if (invalid_connection_info) {
      it.second.remote_id = connection_info.substr(
          0, std::min<size_t>(connection_info.size(), 9));
      it.second.remote_host_name = "unknown";
      it.second.password = "";
      it.second.remember_password = false;
    }

    std::string display_name = GetRecentConnectionDisplayName(it.second);
    bool online = device_presence_.IsOnline(it.second.remote_id);

    ImVec2 image_pos =
        ImVec2(ImGui::GetCursorPosX() + recent_connection_image_width * 0.05f,
               ImGui::GetCursorPosY() + recent_connection_image_height * 0.08f);

    ImGui::SetCursorPos(image_pos);
    ImVec2 image_screen_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(
        (ImTextureID)(intptr_t)it.second.texture,
        ImVec2(recent_connection_image_width, recent_connection_image_height));

    // 必须在 ImGui::Image 后立刻保存 hovered 状态
    const bool image_item_hovered = ImGui::IsItemHovered();

    ImVec2 card_screen_min = image_screen_pos;
    ImVec2 card_screen_max =
        ImVec2(image_screen_pos.x + recent_connection_image_width,
               image_screen_pos.y + recent_connection_image_height +
                   recent_connection_footer_height);

    const bool card_hovered =
        ImGui::IsMouseHoveringRect(card_screen_min, card_screen_max, true);

    // 预先计算 toolbar 区域，即三个按钮所在区域
    const float recent_connection_toolbar_width =
        3.0f * recent_connection_button_width;

    const float recent_connection_toolbar_padding =
        recent_connection_image_width * 0.025f;

    const ImVec2 toolbar_pos = ImVec2(
        image_pos.x + recent_connection_image_width -
            recent_connection_toolbar_width - recent_connection_toolbar_padding,
        image_pos.y + recent_connection_toolbar_padding);

    const ImVec2 toolbar_screen_pos = ImVec2(
        image_screen_pos.x + recent_connection_image_width -
            recent_connection_toolbar_width - recent_connection_toolbar_padding,
        image_screen_pos.y + recent_connection_toolbar_padding);

    const ImVec2 toolbar_screen_end =
        ImVec2(toolbar_screen_pos.x + recent_connection_toolbar_width,
               toolbar_screen_pos.y + recent_connection_button_height);

    const bool toolbar_hovered =
        card_hovered && ImGui::IsMouseHoveringRect(toolbar_screen_pos,
                                                   toolbar_screen_end, true);

    // 关键：鼠标在三个按钮区域时，不显示背景图 tooltip
    const bool show_image_tooltip = image_item_hovered && !toolbar_hovered;

    if (show_image_tooltip) {
      ImGui::BeginTooltip();

      ImGui::SetWindowFontScale(0.5f);

      ImGui::Text("%s", display_name.c_str());

      if (!it.second.remote_host_name.empty() &&
          it.second.remote_host_name != display_name) {
        ImGui::Text("%s", it.second.remote_host_name.c_str());
      }

      ImGui::Text("%s: %s",
                  localization::remote_id[localization_language_index_].c_str(),
                  it.second.remote_id.c_str());

      ImGui::Text("%s",
                  (online ? localization::online[localization_language_index_]
                          : localization::offline[localization_language_index_])
                      .c_str());

      ImGui::SetWindowFontScale(1.0f);

      ImGui::EndTooltip();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImU32 fill_color =
        online ? IM_COL32(0, 255, 0, 255) : IM_COL32(140, 140, 140, 255);

    ImU32 border_color = IM_COL32(255, 255, 255, 255);

    // connection name footer
    {
      ImVec2 footer_pos =
          ImVec2(image_pos.x, image_pos.y + recent_connection_image_height);

      ImVec2 footer_screen_pos =
          ImVec2(image_screen_pos.x,
                 image_screen_pos.y + recent_connection_image_height);

      ImVec2 footer_screen_end =
          ImVec2(footer_screen_pos.x + recent_connection_name_width,
                 footer_screen_pos.y + recent_connection_footer_height);

      float footer_rounding = recent_connection_footer_height * 0.16f;

      draw_list->AddRectFilled(footer_screen_pos, footer_screen_end,
                               IM_COL32(0, 0, 0, 40), footer_rounding,
                               ImDrawFlags_RoundCornersBottom);

      float dot_radius = recent_connection_footer_height * 0.16f;

      ImVec2 footer_dot_pos =
          ImVec2(footer_screen_pos.x + recent_connection_footer_height * 0.45f,
                 footer_screen_pos.y + recent_connection_footer_height * 0.5f);

      draw_list->AddCircleFilled(footer_dot_pos, dot_radius * 1.45f,
                                 border_color, 100);

      draw_list->AddCircleFilled(footer_dot_pos, dot_radius, fill_color, 100);

      ImVec2 text_min =
          ImVec2(footer_dot_pos.x + dot_radius * 2.2f, footer_screen_pos.y);

      ImVec2 text_max =
          ImVec2(footer_screen_end.x - recent_connection_name_width * 0.05f,
                 footer_screen_end.y);

      ImGui::SetWindowFontScale(0.52f);

      ImGui::RenderTextClipped(text_min, text_max, display_name.c_str(),
                               nullptr, nullptr, ImVec2(0.0f, 0.5f));

      ImGui::SetWindowFontScale(1.0f);
    }

    // toolbar / three buttons
    if (card_hovered) {
      float toolbar_rounding = recent_connection_button_height * 0.22f;

      draw_list->AddRectFilled(
          ImVec2(toolbar_screen_pos.x, toolbar_screen_pos.y + 1.0f),
          ImVec2(toolbar_screen_end.x, toolbar_screen_end.y + 1.0f),
          IM_COL32(0, 0, 0, 70), toolbar_rounding);

      draw_list->AddRectFilled(toolbar_screen_pos, toolbar_screen_end,
                               IM_COL32(20, 24, 30, 170), toolbar_rounding);

      draw_list->AddRect(toolbar_screen_pos, toolbar_screen_end,
                         IM_COL32(255, 255, 255, 48), toolbar_rounding);

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, toolbar_rounding);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(0.35f, 0.55f, 0.95f, 0.45f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));

      ImGui::SetWindowFontScale(0.5f);

      // edit alias button
      {
        ImGui::SetCursorPos(toolbar_pos);

        std::string edit = ICON_FA_PEN;
        std::string recent_connection_edit_button_name =
            edit + "##RecentConnectionAlias" + it.first;

        if (ImGui::Button(recent_connection_edit_button_name.c_str(),
                          ImVec2(recent_connection_button_width,
                                 recent_connection_button_height))) {
          BeginEditRecentConnectionAlias(it.second);
        }

        if (ImGui::IsItemHovered()) {
          SetDarkTextTooltip(
              localization::connection_alias[localization_language_index_]
                  .c_str());
        }
      }

      // trash button
      {
        ImVec2 trash_can_button_pos = ImVec2(
            toolbar_pos.x + recent_connection_button_width, toolbar_pos.y);

        ImGui::SetCursorPos(trash_can_button_pos);

        std::string trash_can = ICON_FA_TRASH_CAN;
        std::string recent_connection_delete_button_name =
            trash_can + "##RecentConnectionDelete" + it.first;

        if (ImGui::Button(recent_connection_delete_button_name.c_str(),
                          ImVec2(recent_connection_button_width,
                                 recent_connection_button_height))) {
          show_confirm_delete_connection_ = true;
          delete_connection_name_ = it.first;
        }
        if (ImGui::IsItemHovered()) {
          SetDarkTextTooltip(
              localization::delete_connection[localization_language_index_]
                  .c_str());
        }
      }

      // connect button
      {
        ImVec2 connect_button_pos = ImVec2(
            toolbar_pos.x + 2 * recent_connection_button_width, toolbar_pos.y);

        ImGui::SetCursorPos(connect_button_pos);

        std::string connect = ICON_FA_ARROW_RIGHT_LONG;
        std::string connect_to_this_connection_button_name =
            connect + "##ConnectionTo" + it.first;

        if (ImGui::Button(connect_to_this_connection_button_name.c_str(),
                          ImVec2(recent_connection_button_width,
                                 recent_connection_button_height))) {
          ConnectTo(it.second.remote_id, it.second.password.c_str(),
                    it.second.remember_password);
        }
        if (ImGui::IsItemHovered()) {
          SetDarkTextTooltip(localization::connect_to_this_connection
                                 [localization_language_index_]
                                     .c_str());
        }
      }

      ImGui::SetWindowFontScale(1.0f);

      ImGui::PopStyleColor(4);
      ImGui::PopStyleVar(3);
    }

    if (count != recent_connections_count - 1) {
      ImVec2 line_start =
          ImVec2(image_screen_pos.x + recent_connection_image_width * 1.19f,
                 image_screen_pos.y);

      ImVec2 line_end =
          ImVec2(image_screen_pos.x + recent_connection_image_width * 1.19f,
                 image_screen_pos.y + recent_connection_image_height +
                     recent_connection_footer_height);

      ImGui::GetWindowDrawList()->AddLine(line_start, line_end,
                                          IM_COL32(0, 0, 0, 122), 1.0f);
    }

    if (delete_connection_ && delete_connection_name_ == it.first) {
      if (!thumbnail_->DeleteThumbnail(it.first)) {
        recent_connection_aliases_.erase(it.second.remote_id);
        SaveRecentConnectionAliases();
        reload_recent_connections_ = true;
        delete_connection_ = false;
      }
    }

    ImGui::EndChild();

    if (count != recent_connections_count - 1) {
      ImVec2 line_start =
          ImVec2(image_screen_pos.x + recent_connection_image_width * 1.19f,
                 image_screen_pos.y);
      ImVec2 line_end =
          ImVec2(image_screen_pos.x + recent_connection_image_width * 1.19f,
                 image_screen_pos.y + recent_connection_image_height +
                     recent_connection_footer_height);
      ImGui::GetWindowDrawList()->AddLine(line_start, line_end,
                                          IM_COL32(0, 0, 0, 122), 1.0f);
    }

    count++;
    ImGui::SameLine(0, count != recent_connections_count
                           ? (recent_connection_image_width * 0.165f)
                           : 0.0f);
  }

  ImGui::EndChild();

  if (show_confirm_delete_connection_) {
    ConfirmDeleteConnection();
  }
  if (show_edit_connection_alias_window_) {
    EditRecentConnectionAliasWindow();
  }
  if (show_offline_warning_window_) {
    OfflineWarningWindow();
  }

  return 0;
}

int Render::ConfirmDeleteConnection() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));
  ImGui::SetNextWindowSize(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);

  ImGui::Begin("ConfirmDeleteConnectionWindow", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  auto connection_status_window_width = ImGui::GetWindowSize().x;
  auto connection_status_window_height = ImGui::GetWindowSize().y;

  std::string text =
      localization::confirm_delete_connection[localization_language_index_];
  ImGui::SetCursorPosX(connection_status_window_width * 0.33f);
  ImGui::SetCursorPosY(connection_status_window_height * 0.67f);

  // ok
  ImGui::SetWindowFontScale(0.5f);
  if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
      ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    delete_connection_ = true;
    show_confirm_delete_connection_ = false;
  }

  ImGui::SameLine();
  // cancel
  if (ImGui::Button(
          localization::cancel[localization_language_index_].c_str()) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    delete_connection_ = false;
    show_confirm_delete_connection_ = false;
  }

  auto text_width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SetCursorPosX((connection_status_window_width - text_width) * 0.5f);
  ImGui::SetCursorPosY(connection_status_window_height * 0.2f);
  ImGui::Text("%s", text.c_str());
  ImGui::SetWindowFontScale(1.0f);

  ImGui::End();
  ImGui::PopStyleVar();
  return 0;
}

int Render::EditRecentConnectionAliasWindow() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));
  ImGui::SetNextWindowSize(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);

  ImGui::Begin("EditRecentConnectionAliasWindow", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  auto window_width = ImGui::GetWindowSize().x;
  auto window_height = ImGui::GetWindowSize().y;
  std::string text =
      localization::input_connection_alias[localization_language_index_];

  ImGui::SetWindowFontScale(0.5f);
  auto text_width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
  ImGui::SetCursorPosY(window_height * 0.2f);
  ImGui::Text("%s", text.c_str());

  ImGui::SetCursorPosX(window_width * 0.2f);
  ImGui::SetCursorPosY(window_height * 0.4f);
  ImGui::SetNextItemWidth(window_width * 0.6f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

  if (focus_on_input_widget_) {
    ImGui::SetKeyboardFocusHere();
    focus_on_input_widget_ = false;
  }

  bool enter_pressed =
      ImGui::InputText("##recent_connection_alias", edit_connection_alias_,
                       IM_ARRAYSIZE(edit_connection_alias_),
                       ImGuiInputTextFlags_EnterReturnsTrue);

  ImGui::PopStyleVar();

  ImGui::SetCursorPosX(window_width * 0.315f);
  ImGui::SetCursorPosY(window_height * 0.75f);

  if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
      enter_pressed) {
    std::string alias = TrimConnectionAlias(edit_connection_alias_);
    if (alias.empty()) {
      recent_connection_aliases_.erase(edit_connection_alias_remote_id_);
    } else {
      recent_connection_aliases_[edit_connection_alias_remote_id_] = alias;
    }

    SaveRecentConnectionAliases();
    show_edit_connection_alias_window_ = false;
    focus_on_input_widget_ = true;
    memset(edit_connection_alias_, 0, sizeof(edit_connection_alias_));
    edit_connection_alias_remote_id_.clear();
  }

  ImGui::SameLine();

  if (ImGui::Button(
          localization::cancel[localization_language_index_].c_str()) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    show_edit_connection_alias_window_ = false;
    focus_on_input_widget_ = true;
    memset(edit_connection_alias_, 0, sizeof(edit_connection_alias_));
    edit_connection_alias_remote_id_.clear();
  }

  ImGui::SetWindowFontScale(1.0f);

  ImGui::End();
  ImGui::PopStyleVar();
  return 0;
}

int Render::OfflineWarningWindow() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));
  ImGui::SetNextWindowSize(
      ImVec2(io.DisplaySize.x * 0.33f, io.DisplaySize.y * 0.33f));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, window_rounding_ * 0.5f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, window_rounding_);

  ImGui::Begin("OfflineWarningWindow", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  auto window_width = ImGui::GetWindowSize().x;
  auto window_height = ImGui::GetWindowSize().y;

  ImGui::SetCursorPosX(window_width * 0.43f);
  ImGui::SetCursorPosY(window_height * 0.67f);
  ImGui::SetWindowFontScale(0.5f);
  if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
      ImGui::IsKeyPressed(ImGuiKey_Enter) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    show_offline_warning_window_ = false;
  }

  auto text_width = ImGui::CalcTextSize(offline_warning_text_.c_str()).x;
  ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
  ImGui::SetCursorPosY(window_height * 0.2f);
  ImGui::Text("%s", offline_warning_text_.c_str());
  ImGui::SetWindowFontScale(1.0f);

  ImGui::End();
  ImGui::PopStyleVar();
  return 0;
}
}  // namespace crossdesk
