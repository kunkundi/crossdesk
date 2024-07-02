#include "layout_style.h"
#include "localization.h"
#include "log.h"
#include "render.h"

int Render::ConnectionStatusWindow() {
  if (connect_button_pressed_) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2((viewport->WorkSize.x - viewport->WorkPos.x -
                                    connection_status_window_width_) /
                                       2,
                                   (viewport->WorkSize.y - viewport->WorkPos.y -
                                    connection_status_window_height_) /
                                       2));

    ImGui::SetNextWindowSize(ImVec2(connection_status_window_width_,
                                    connection_status_window_height_));

    ImGui::SetWindowFontScale(0.5f);
    ImGui::Begin("ConnectionStatusWindow", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SetCursorPosX(connection_status_window_width_ * 1 / 3);
    ImGui::SetCursorPosY(connection_status_window_height_ * 1 / 3);
    ImGui::SetWindowFontScale(0.5f);

    if (ConnectionStatus::Connecting == connection_status_) {
      ImGui::Text("Connecting...");
    } else if (ConnectionStatus::Connected == connection_status_) {
      ImGui::Text("Connected");
    } else if (ConnectionStatus::Disconnected == connection_status_) {
      ImGui::Text("Disonnected");
    } else if (ConnectionStatus::Failed == connection_status_) {
      ImGui::Text("Failed");
    } else if (ConnectionStatus::Closed == connection_status_) {
      ImGui::Text("Closed");
    } else if (ConnectionStatus::IncorrectPassword == connection_status_) {
      ImGui::Text("Incorrect password");
    } else if (ConnectionStatus::NoSuchTransmissionId == connection_status_) {
      ImGui::Text("No such transmissionId");
    }

    ImGui::SetWindowFontScale(1.0f);

    ImGui::SetCursorPosX(connection_status_window_width_ * 3 / 7);
    ImGui::SetCursorPosY(connection_status_window_height_ * 2 / 3);

    ImGui::SetWindowFontScale(0.5f);
    // Cancel
    if (ImGui::Button(
            localization::cancel[localization_language_index_].c_str())) {
      connect_button_pressed_ = false;
    }
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SetWindowFontScale(0.5f);
    ImGui::End();
    ImGui::SetWindowFontScale(1.0f);
  }
  return 0;
}