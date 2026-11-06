#include "runtime/gui_runtime.h"

#include "platform.h"
#include "rd_log.h"

namespace crossdesk {
void GuiRuntime::InitializeTrustedDevices(const std::filesystem::path& directory) {
  try {
    trusted_devices_ = std::make_unique<TrustedDevices>(directory, cache_path_);
  } catch (const std::exception& error) {
    LOG_ERROR("Cannot load trusted devices: {}", error.what());
    // The independent persisted switch stays enabled on identity/store failure.
  }
}

bool GuiRuntime::TrustedControllerAllowed(const std::string& id) {
  if (!config_center_ || !config_center_->IsTrustedDevicesOnly()) return true;
  std::lock_guard lock(controller_trust_mutex_);
  const auto it = controller_trust_.find(id);
  return it != controller_trust_.end() && it->second.IsTrusted(trusted_devices_.get());
}

bool GuiRuntime::BeginControllerTrust(const std::string& id, PeerEventHandler* events) {
  const bool required = config_center_->IsTrustedDevicesOnly();
  std::lock_guard lock(controller_trust_mutex_);
  const auto previous = controller_trust_.find(id);
  // Non-connected statuses clear this state. Duplicate Connected notifications
  // in the same application session must not restart the challenge or timeout.
  if (previous != controller_trust_.end() && previous->second.events == events) {
    auto& state = previous->second;
    const bool allowed = !required || state.IsTrusted(trusted_devices_.get());
    state.pending = !allowed;
    return allowed;
  }
  ControllerTrust state;
  state.events = events;
  state.pending = required;
  state.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  try {
    if (!trusted_devices_) state.invalid = true;
    else state.nonce = TrustedDevices::Nonce();
  } catch (...) { state.invalid = true; }
  controller_trust_[id] = std::move(state);
  return !required;
}

void GuiRuntime::ForgetControllerTrust(const std::string& id, PeerEventHandler* events) {
  std::lock_guard lock(controller_trust_mutex_);
  const auto it = controller_trust_.find(id);
  if (it != controller_trust_.end() && it->second.events == events)
    controller_trust_.erase(it);
}

int GuiRuntime::SendTrustMessage(const std::string& id, const nlohmann::json& message) {
  const auto text = message.dump();
  return WithController(id, [&](MiniRtcPeerPtr* peer, const std::string& wire) {
    return MiniRtcSendReliableDataFrameToPeer(peer, text.data(), text.size(),
        control_data_label_.c_str(), wire.data(), wire.size());
  }, false);
}

bool GuiRuntime::HandleDeviceTrustMessage(const std::string& id, PeerEventHandler* events,
                                         const char* data, size_t size) {
  if (!data || size > 4096) return false;
  const auto message = nlohmann::json::parse(data, data + size, nullptr, false);
  if (!message.is_object()) return false;
  const auto type_it = message.find("type");
  if (type_it == message.end()) return false;
  if (*type_it == static_cast<int>(ControlType::host_infomation)) {
    // Advertise through an existing message so disabled restrictions send no
    // unfamiliar protocol messages to legacy clients. Retain only bounded,
    // inert metadata until the connection's proof has been accepted.
    std::lock_guard lock(controller_trust_mutex_);
    const auto it = controller_trust_.find(id);
    if (it != controller_trust_.end() && it->second.events == events) {
      const auto capability = message.find("device_trust_version");
      if (capability != message.end() && *capability == 1) it->second.capable = true;
      if (it->second.pending) {
        it->second.early_host_info.assign(data, size);
        return true;
      }
    }
    return false;
  }
  if (!type_it->is_string()) return false;
  const auto type = type_it->get<std::string>();
  if (type.rfind("device_trust_", 0) != 0) return false;
  if (!trusted_devices_) return true;
  try {
    if (type == "device_trust_challenge") {
      const auto props = FindRemoteSession(id);
      if (!props || props->closing_ || props->peer_events_.get() != events ||
          message.at("version") != 1) return true;
      const auto wire = events->WireId(id);
      const auto proof = trusted_devices_->Prove(message.at("nonce"),
          props->local_id_, GetHostName()).dump();
      MiniRtcSendReliableDataFrameToPeer(props->peer_, proof.data(), proof.size(),
          control_data_label_.c_str(), wire.data(), wire.size());
    } else if (type == "device_trust_proof") {
      std::lock_guard lock(controller_trust_mutex_);
      const auto it = controller_trust_.find(id);
      if (it == controller_trust_.end() || it->second.events != events) return true;
      auto& state = it->second;
      if (state.invalid || state.verified || std::chrono::steady_clock::now() >= state.deadline)
        return true;
      // Accept once per fresh challenge; malformed/replayed proofs grant nothing.
      state.verified = TrustedDevices::Verify(message, state.nonce);
      if (!state.verified) state.invalid = true;
    }
  } catch (const std::exception&) {
    // Untrusted protocol input never escapes the transport callback.
  }
  return true;
}

void GuiRuntime::TickControllerTrust() {
  struct Action {
    std::string id;
    PeerEventHandler* events;
    std::string nonce;
    int kind;
    std::string host_info;
  };
  std::vector<Action> actions;
  const auto now = std::chrono::steady_clock::now();
  const bool required = config_center_->IsTrustedDevicesOnly();
  {
    std::lock_guard lock(controller_trust_mutex_);
    for (auto& [id, state] : controller_trust_) {
      if (state.closing) continue;
      const bool trusted = state.IsTrusted(trusted_devices_.get());
      if (required && (state.invalid || (state.verified && !trusted) ||
                       (!state.verified && now >= state.deadline))) {
        state.invalid = true;
        state.closing = true;
        actions.push_back({id, state.events, {}, -1});
      } else if (state.pending && (!required || trusted)) {
        state.pending = false;
        actions.push_back({id, state.events, {}, 1, std::exchange(state.early_host_info, {})});
      }
      if ((required || state.capable) && !state.invalid && !state.verified &&
          now < state.deadline && now >= state.next_challenge) {
        state.next_challenge = now + std::chrono::seconds(1);
        actions.push_back({id, state.events, state.nonce, 0});
      }
    }
  }
  for (const auto& action : actions) {
    const auto events = ControllerEvents(action.id);
    if (!events || events.get() != action.events) continue;
    auto callback = events->EnterCallback();
    if (!callback) continue;
    if (action.kind < 0) {
      LOG_WARN("Reject controller [{}]: trusted device verification required", action.id);
      transfers_.Cancel(events.get(), action.id);
      remote_input_.Disconnect(action.id);
      CloseServerController(action.id);
    } else if (action.kind > 0) {
      events->ApplyConnectionStatus(MiniRtcConnectionStatus::MiniRtcConnectionConnected, action.id);
      if (!action.host_info.empty()) {
        const auto wire = events->WireId(action.id);
        PeerEventHandler::OnReceiveDataBuffer(action.host_info.data(), action.host_info.size(),
            wire.data(), wire.size(), control_data_label_.data(), control_data_label_.size(), events.get());
      }
    } else {
      SendTrustMessage(action.id, {{"type", "device_trust_challenge"},
          {"version", 1}, {"nonce", action.nonce}});
    }
  }
}

std::optional<TrustedDevices::Device> GuiRuntime::VerifiedController(const std::string& id) {
  std::lock_guard lock(controller_trust_mutex_);
  const auto it = controller_trust_.find(id);
  return it == controller_trust_.end() || it->second.invalid
      ? std::nullopt : it->second.verified;
}
bool GuiRuntime::TrustController(const std::string& id, const std::string& expected_key) {
  if (!trusted_devices_ || !IsAuthorizedController(id)) return false;
  const auto device = VerifiedController(id);
  if (!device || device->public_key != expected_key) return false;
  try { trusted_devices_->Add(*device); return true; }
  catch (const std::exception& error) { LOG_ERROR("Cannot trust device: {}", error.what()); return false; }
}
bool GuiRuntime::RemoveTrustedDevice(const std::string& public_key) {
  if (!trusted_devices_) return false;
  try { trusted_devices_->Remove(public_key); }
  catch (const std::exception& error) { LOG_ERROR("Cannot remove trusted device: {}", error.what()); return false; }
  TickControllerTrust();
  return true;
}
}  // namespace crossdesk
