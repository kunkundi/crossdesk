#include "runtime/device_login.h"

#include <algorithm>
#include <cstring>

#include "runtime/trusted_devices.h"

namespace crossdesk {
int DeviceLogin::Exchange(SignalExchangeStage stage, const nlohmann::json& message,
    const TrustedDevices* identity, char* response, size_t capacity) {
  auto output = [&](const nlohmann::json& reply) {
    const auto text = reply.dump();
    if (text.empty() || text.size() > capacity) return -1;
    std::memcpy(response, text.data(), text.size());
    return static_cast<int>(text.size());
  };
  if (stage == SignalExchangeStage::PrepareLogin) {
    phase_ = Phase::Idle;
    used_password_ = false;
    password_.clear();
    request_ = message;
    const auto login = message.at("user_id").get<std::string>();
    if (!identity) return -1;  // Never fall back when local key storage fails.
    const auto at = login.find('@');
    request_["user_id"] = login.substr(0, at);
    if (at != std::string::npos) password_ = login.substr(at + 1);
    request_["device_key"] = {{"version", 1}, {"public_key", identity->PublicKey()}};
    request_.erase("reconnect_token");
    request_.erase("reconnect_version");
    phase_ = Phase::AwaitingChallenge;
    return output(request_);
  }
  if (!Pending()) return 0;
  const auto type = message.value("type", "");
  if (type == "login_challenge") {
    if (!identity || phase_ != Phase::AwaitingChallenge || message.at("device_key_version") != 1 ||
        message.at("user_id") != request_.at("user_id")) return -1;
    const auto nonce = message.at("nonce").get<std::string>();
    if (nonce.size() != 64 || !std::all_of(nonce.begin(), nonce.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
      return -1;
    const auto id = request_.at("user_id").get<std::string>();
    auto reply = request_;
    used_password_ = message.at("password_required").get<bool>();
    if (used_password_) {
      if (id.empty() || id.rfind("C-", 0) == 0 || password_.empty()) return -1;
      reply["user_id"] = id + "@" + password_;
    }
    const auto transcript = nlohmann::json::array(
        {"crossdesk.device-login.v1", id, identity->PublicKey(), nonce}).dump();
    reply["device_key"]["nonce"] = nonce;
    reply["device_key"]["signature"] = identity->Sign(transcript);
    phase_ = Phase::AwaitingResult;
    password_.clear();
    return output(reply);
  }
  if (type == "login") {
    if (message.at("status") == "success") {
      if (phase_ != Phase::AwaitingResult || message.value("device_key_version", 0) != 1) return -1;
      const auto assigned = message.at("user_id").get<std::string>();
      const auto expected = request_.at("user_id").get<std::string>();
      if (assigned.empty() || (!expected.empty() && assigned != expected)) return -1;
    }
    password_.clear();
    request_.clear();
    phase_ = Phase::Idle;
  }
  return 0;
}
}  // namespace crossdesk
