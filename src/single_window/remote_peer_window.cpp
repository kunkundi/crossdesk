#include "layout_style.h"
#include "localization.h"
#include "rd_log.h"
#include "render.h"

static int InputTextCallback(ImGuiInputTextCallbackData *data);

int Render::RemoteWindow() {
  ImGui::SetNextWindowPos(ImVec2(local_window_width_ + 1.0f, title_bar_height_),
                          ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::BeginChild("RemoteDesktopWindow",
                    ImVec2(remote_window_width_, remote_window_height_),
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + main_window_text_y_padding_);
  ImGui::Indent(main_child_window_x_padding_ - 1.0f);

  ImGui::TextColored(
      ImVec4(0.0f, 0.0f, 0.0f, 0.5f), "%s",
      localization::remote_desktop[localization_language_index_].c_str());

  ImGui::Spacing();
  {
    ImGui::SetNextWindowPos(
        ImVec2(local_window_width_ + main_child_window_x_padding_ - 1.0f,
               title_bar_height_ + main_child_window_y_padding_),
        ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(239.0f / 255, 240.0f / 255,
                                                   242.0f / 255, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);

    ImGui::BeginChild(
        "RemoteDesktopWindow_1",
        ImVec2(remote_child_window_width_, remote_child_window_height_),
        ImGuiChildFlags_Border,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
      ImGui::SetWindowFontScale(0.8f);
      ImGui::Text(
          "%s", localization::remote_id[localization_language_index_].c_str());

      ImGui::Spacing();
      ImGui::SetNextItemWidth(IPUT_WINDOW_WIDTH);
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      if (re_enter_remote_id_) {
        ImGui::SetKeyboardFocusHere();
        re_enter_remote_id_ = false;
        memset(remote_id_display_, 0, sizeof(remote_id_display_));
      }
      bool enter_pressed = ImGui::InputText(
          "##remote_id_", remote_id_display_, IM_ARRAYSIZE(remote_id_display_),
          ImGuiInputTextFlags_CharsDecimal |
              ImGuiInputTextFlags_EnterReturnsTrue |
              ImGuiInputTextFlags_CallbackEdit,
          InputTextCallback);

      ImGui::PopStyleVar();
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_ARROW_RIGHT_LONG, ImVec2(55, 38)) ||
          enter_pressed) {
        connect_button_pressed_ = true;
        remote_id_ = remote_id_display_;
        remote_id_.erase(remove_if(remote_id_.begin(), remote_id_.end(),
                                   static_cast<int (*)(int)>(&isspace)),
                         remote_id_.end());
        ConnectTo(remote_id_, remote_password_, false);
      }

      std::string client_id = "C-" + remote_id_;
      if (client_properties_.find(client_id) != client_properties_.end()) {
        auto props = client_properties_[client_id];
        if (props->rejoin_) {
          ConnectTo(remote_id_, remote_password_,
                    client_properties_[remote_id_]->remember_password_);
        }
      }
    }
    ImGui::EndChild();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();

  return 0;
}

static int InputTextCallback(ImGuiInputTextCallbackData *data) {
  if (data->BufTextLen > 3 && data->Buf[3] != ' ') {
    data->InsertChars(3, " ");
  }

  if (data->BufTextLen > 7 && data->Buf[7] != ' ') {
    data->InsertChars(7, " ");
  }

  return 0;
}

int Render::ConnectTo(const std::string &host_name, const char *password,
                      bool remember_password) {
  LOG_INFO("Connect to [{}]", host_name);

  std::string client_id = "C-" + host_name;
  if (client_properties_.find(client_id) == client_properties_.end()) {
    client_properties_[client_id] =
        std::make_shared<SubStreamWindowProperties>();
  }

  auto props = client_properties_[client_id];
  props->connection_status_ = ConnectionStatus::Connecting;
  props->remember_password_ = remember_password;
  memcpy(props->remote_password_, password, 6);

  int ret = -1;

  if (!props->connection_established_) {
    if (peer_map_.find(client_id) == peer_map_.end()) {
      memcpy(&props->params_, &params_, sizeof(Params));
      props->params_.user_id = client_id.c_str();
      peer_map_[client_id] = CreatePeer(&props->params_);
    }

    if (peer_map_[client_id]) {
      LOG_INFO("[{}] Create peer instance successful", client_id);
      Init(peer_map_[client_id]);
      LOG_INFO("[{}] Peer init finish", client_id);
    } else {
      LOG_INFO("Create peer [{}] instance failed", client_id);
    }

    ret = JoinConnection(peer_map_[client_id], host_name.c_str(), password);
    if (0 == ret) {
      is_client_mode_ = true;
      props->rejoin_ = false;
    } else {
      props->rejoin_ = true;
    }
  }

  return 0;
}