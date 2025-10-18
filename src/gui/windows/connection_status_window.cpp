#include "layout.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

bool Render::ConnectionStatusWindow(
    std::shared_ptr<SubStreamWindowProperties> &props) {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  bool ret_flag = false;
  ImGui::SetNextWindowPos(ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                                  connection_status_window_width_) /
                                     2,
                                 (viewport->WorkSize.y - viewport->WorkPos.y -
                                  connection_status_window_height_) /
                                     2));

  ImGui::SetNextWindowSize(ImVec2(connection_status_window_width_,
                                  connection_status_window_height_));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0, 1.0, 1.0, 1.0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);

  ImGui::Begin("ConnectionStatusWindow", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  ImGui::SetWindowFontScale(0.5f);
  std::string text;

  if (ConnectionStatus::Connecting == props->connection_status_) {
    text = localization::p2p_connecting[localization_language_index_];
    ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
    ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);
  } else if (ConnectionStatus::Connected == props->connection_status_) {
    text = localization::p2p_connected[localization_language_index_];
    ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
    ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);
    // ok
    if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_connection_status_window_ = false;
    }
  } else if (ConnectionStatus::Disconnected == props->connection_status_) {
    text = localization::p2p_disconnected[localization_language_index_];
    ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
    ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);
    // ok
    if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_connection_status_window_ = false;
    }
  } else if (ConnectionStatus::Failed == props->connection_status_) {
    text = localization::p2p_failed[localization_language_index_];
    ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
    ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);
    // ok
    if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_connection_status_window_ = false;
    }
  } else if (ConnectionStatus::Closed == props->connection_status_) {
    text = localization::p2p_closed[localization_language_index_];
    ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
    ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);
    // ok
    if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_connection_status_window_ = false;
    }
  } else if (ConnectionStatus::IncorrectPassword == props->connection_status_) {
    if (!password_validating_) {
      if (password_validating_time_ == 1) {
        text = localization::input_password[localization_language_index_];
      } else {
        text = localization::reinput_password[localization_language_index_];
      }

      auto window_width = ImGui::GetWindowSize().x;
      auto window_height = ImGui::GetWindowSize().y;
      ImGui::SetCursorPosX((window_width - IPUT_WINDOW_WIDTH / 2) * 0.5f);
      ImGui::SetCursorPosY(window_height * 0.4f);
      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH / 2);

      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

      if (focus_on_input_widget_) {
        ImGui::SetKeyboardFocusHere();
        focus_on_input_widget_ = false;
      }

      ImGui::InputText("##password", props->remote_password_,
                       IM_ARRAYSIZE(props->remote_password_),
                       ImGuiInputTextFlags_CharsNoBlank);

      ImGui::SetWindowFontScale(0.4f);

      ImVec2 text_size = ImGui::CalcTextSize(
          localization::remember_password[localization_language_index_]
              .c_str());
      ImGui::SetCursorPosX((window_width - text_size.x) * 0.5f - 13.0f);
      ImGui::Checkbox(
          localization::remember_password[localization_language_index_].c_str(),
          &(props->remember_password_));
      ImGui::SetWindowFontScale(0.5f);
      ImGui::PopStyleVar();

      ImGui::SetCursorPosX(window_width * 0.315f);
      ImGui::SetCursorPosY(window_height * 0.75f);
      // ok
      if (ImGui::Button(
              localization::ok[localization_language_index_].c_str()) ||
          ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        show_connection_status_window_ = true;
        password_validating_ = true;
        props->rejoin_ = true;
        need_to_rejoin_ = true;
        focus_on_input_widget_ = true;
      }

      ImGui::SameLine();
      // cancel
      if (ImGui::Button(
              localization::cancel[localization_language_index_].c_str()) ||
          ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        memset(props->remote_password_, 0, sizeof(props->remote_password_));
        show_connection_status_window_ = false;
        focus_on_input_widget_ = true;
      }
    } else if (password_validating_) {
      text = localization::validate_password[localization_language_index_];
      ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
      ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);
    }
  } else if (ConnectionStatus::NoSuchTransmissionId ==
             props->connection_status_) {
    text = localization::no_such_id[localization_language_index_];
    ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
    ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);
    // ok
    if (ImGui::Button(localization::ok[localization_language_index_].c_str()) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      show_connection_status_window_ = false;
      re_enter_remote_id_ = true;
      DestroyPeer(&props->peer_);
      ret_flag = true;
    }
  }

  auto window_width = ImGui::GetWindowSize().x;
  auto window_height = ImGui::GetWindowSize().y;
  auto text_width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
  ImGui::SetCursorPosY(window_height * 0.2f);
  ImGui::Text("%s", text.c_str());
  ImGui::SetWindowFontScale(1.0f);

  ImGui::End();
  ImGui::PopStyleVar();

  return ret_flag;
}