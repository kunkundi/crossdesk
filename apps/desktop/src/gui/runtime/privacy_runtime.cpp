/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include <cstring>
#include <shared_mutex>

#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

bool GuiRuntime::IsAuthorizedController(const std::string& remote_id) {
  // Connections appear here only after the existing password/signalling/RTC
  // admission path. Outgoing/viewer sessions live in remote_sessions_ instead.
  std::shared_lock lock(connection_status_mutex_);
  const auto found = connection_status_.find(remote_id);
  return found != connection_status_.end() &&
         found->second == ConnectionStatus::Connected;
}

void GuiRuntime::SetControllerPrivacySupport(const std::string& remote_id,
                                           bool supported) {
  std::unique_lock lock(connection_status_mutex_);
  const auto found = connection_status_.find(remote_id);
  if (found == connection_status_.end()) return;
  if (found->second != ConnectionStatus::Connected) {
    if (found->second == ConnectionStatus::Connecting ||
        found->second == ConnectionStatus::Gathering) {
      privacy_sessions_.RememberSupport(remote_id, supported);
      LOG_INFO("Privacy: retained early controller capability, peer={}, supported={}",
               remote_id, supported);
    }
    return;
  }

  const bool automatic_enable =
      privacy_sessions_.SetSupported(remote_id, supported);
  ApplyPrivacyAdmission(automatic_enable &&
                        config_center_->IsEnablePrivacyScreen());
}

void GuiRuntime::ApplyPrivacyAdmission(bool automatic_enable) {
  if (!privacy_sessions_.CanEnable()) {
    if (privacy_.Engaged()) privacy_.Disable();
  } else if (automatic_enable) {
    privacy_.EnableOnConnection();
  }
}

void GuiRuntime::QueuePrivacyCommand(const std::string& remote_id,
                                     const PrivacyCommand& command) {
  std::shared_lock session_lock(connection_status_mutex_);
  const auto found = connection_status_.find(remote_id);
  if (found == connection_status_.end() ||
      found->second != ConnectionStatus::Connected ||
      !privacy_sessions_.Supports(remote_id)) return;
  std::lock_guard lock(privacy_commands_mutex_);
  if (privacy_commands_.size() >= 32) return;
  LOG_INFO("Privacy: queued controller command, peer={}, flag={}", remote_id,
           static_cast<int>(command.flag));
  privacy_commands_.emplace_back(remote_id, command);
}

void GuiRuntime::HandlePrivacy() {
  std::deque<std::pair<std::string, PrivacyCommand>> commands;
  {
    std::lock_guard lock(privacy_commands_mutex_);
    commands.swap(privacy_commands_);
  }
  for (const auto& [remote_id, command] : commands) {
    // Keep admission and activation under the connection lock so an unknown
    // controller cannot join between the capability check and enabling.
    std::shared_lock lock(connection_status_mutex_);
    const auto found = connection_status_.find(remote_id);
    if (found == connection_status_.end() ||
        found->second != ConnectionStatus::Connected ||
        !privacy_sessions_.Supports(remote_id)) continue;
    if (command.flag == PrivacyCommandFlag::enable) {
      if (privacy_sessions_.CanEnable())
        privacy_.Enable(command.block_local_input);
    } else if (command.flag == PrivacyCommandFlag::disable) {
      privacy_.Disable();
    }
    last_privacy_status_tick_ = 0;
  }
  PrivacyStatus status{};
  const uint64_t now = SDL_GetTicks();
  std::vector<std::string> controllers;
  {
    std::shared_lock lock(connection_status_mutex_);
    status = privacy_.Snapshot();
    for (const auto& [id, state] : connection_status_)
      if (state == ConnectionStatus::Connected &&
          privacy_sessions_.Supports(id))
        controllers.push_back(id);
    if (!privacy_sessions_.CanEnable()) {
      status.state = PrivacyState::unsupported;
      status.supported = false;
      status.input_block_supported = false;
      std::strcpy(status.reason,
                  "A connected controller does not support privacy screen");
    }
  }
  if (!peer_ || (last_privacy_status_tick_ != 0 &&
                 status.revision == last_privacy_revision_ &&
                 now - last_privacy_status_tick_ < 1000))
    return;
  RemoteAction action{};
  action.type = ControlType::privacy_status;
  action.ps = status;
  const auto message = action.to_json();
  bool sent = true;
  for (const auto& id : controllers) {
    const int result = SendReliableDataFrameToPeer(
        peer_, message.data(), message.size(), control_data_label_.c_str(),
        id.data(), id.size());
    if (result != 0) {
      sent = false;
      LOG_WARN("Privacy status send failed, peer={}, ret={}", id, result);
    }
  }
  if (sent) {
    last_privacy_revision_ = status.revision;
    last_privacy_status_tick_ = now;
  }
}

}  // namespace crossdesk
