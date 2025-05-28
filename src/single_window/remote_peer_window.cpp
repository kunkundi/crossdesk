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

      std::string remote_id = remote_id_display_;
      remote_id.erase(remove_if(remote_id.begin(), remote_id.end(),
                                static_cast<int (*)(int)>(&isspace)),
                      remote_id.end());
      if (ImGui::Button(ICON_FA_ARROW_RIGHT_LONG, ImVec2(55, 38)) ||
          enter_pressed) {
        connect_button_pressed_ = true;
        bool found = false;
        for (auto &[id, props] : recent_connections_) {
          if (id.find(remote_id) != std::string::npos) {
            found = true;
            if (client_properties_.find(remote_id) !=
                client_properties_.end()) {
              if (!client_properties_[remote_id]->connection_established_) {
                ConnectTo(props.remote_id, props.password.c_str(), false);
              } else {
                // todo: show warning message
                LOG_INFO("Already connected to [{}]", remote_id);
              }
            } else {
              ConnectTo(props.remote_id, props.password.c_str(), false);
            }
          }
        }

        if (!found) {
          ConnectTo(remote_id, "", false);
        }
      }

      if (need_to_rejoin_) {
        need_to_rejoin_ = false;
        for (const auto &[_, props] : client_properties_) {
          if (props->rejoin_) {
            ConnectTo(props->remote_id_, props->remote_password_,
                      props->remember_password_);
          }
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

int Render::ConnectTo(const std::string &remote_id, const char *password,
                      bool remember_password) {
  LOG_INFO("Connect to [{}]", remote_id);

  if (client_properties_.find(remote_id) == client_properties_.end()) {
    client_properties_[remote_id] =
        std::make_shared<SubStreamWindowProperties>();
    auto props = client_properties_[remote_id];
    props->local_id_ = "C-" + remote_id;
    props->remote_id_ = remote_id;
    memcpy(&props->params_, &params_, sizeof(Params));
    props->params_.user_id = props->local_id_.c_str();
    props->peer_ = CreatePeer(&props->params_);
    AddAudioStream(props->peer_, props->audio_label_.c_str());
    AddDataStream(props->peer_, props->data_label_.c_str());

    if (props->peer_) {
      LOG_INFO("[{}] Create peer instance successful", props->local_id_);
      Init(props->peer_);
      LOG_INFO("[{}] Peer init finish", props->local_id_);
    } else {
      LOG_INFO("Create peer [{}] instance failed", props->local_id_);
    }

    props->connection_status_ = ConnectionStatus::Connecting;
  }
  int ret = -1;
  auto props = client_properties_[remote_id];
  if (!props->connection_established_) {
    props->remember_password_ = remember_password;
    if (strcmp(password, "") != 0 &&
        strcmp(password, props->remote_password_) != 0) {
      strncpy(props->remote_password_, password,
              sizeof(props->remote_password_) - 1);
      props->remote_password_[sizeof(props->remote_password_) - 1] = '\0';
    }
    ret = JoinConnection(props->peer_, remote_id.c_str(), password);
    if (0 == ret) {
      props->rejoin_ = false;
    } else {
      props->rejoin_ = true;
      need_to_rejoin_ = true;
    }
  }

  return 0;
}