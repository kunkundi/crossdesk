#include "controller_session.h"
#include <stream_names.h>

namespace crossdesk {
using nlohmann::json;

uint64_t ControllerSession::RequestRemote(const std::string &method, json arguments) {
  if (!Connected() || remote_results_.size() >= 512) return 0;
  const auto id = ++remote_sequence_;
  remote_results_[id] = nullptr;
  const auto message = json{{"type", "automation"}, {"version", 1},
      {"id", id}, {"method", method}, {"arguments", std::move(arguments)}}.dump();
  if (!Send(kControlStream, message)) {
    remote_results_.erase(id);
    return 0;
  }
  return id;
}

std::optional<json> ControllerSession::TakeRemoteResult(uint64_t request) {
  const auto found = remote_results_.find(request);
  if (found == remote_results_.end() || found->second.is_null()) return {};
  auto result = std::move(found->second);
  remote_results_.erase(found);
  return result;
}

void ControllerSession::BeginInput(bool acknowledge) {
  action_keys_ = keys_;
  action_buttons_ = buttons_;
  for (const auto id : input_requests_) remote_results_.erase(id);
  input_requests_.clear();
  input_result_ = "applied";
  input_ack_transport_ = InputAcknowledged();
  input_ack_active_ = acknowledge && input_ack_transport_;
}

bool ControllerSession::SendInput(const char *stream, const RemoteAction &action) {
  if (!input_ack_transport_) return Send(stream, action.to_json());
  const auto id = RequestRemote("input", json::parse(action.to_json()));
  if (!id) return false;
  if (input_ack_active_) input_requests_.push_back(id);
  else remote_results_.erase(id);
  return true;
}

std::string ControllerSession::InputResult() {
  if (!input_ack_active_) return "unsupported";
  for (auto it = input_requests_.begin(); it != input_requests_.end();) {
    auto reply = TakeRemoteResult(*it);
    if (!reply) { ++it; continue; }
    const auto result = reply->value("result", json{});
    if (reply->value("ok", json{}) != true || !result.is_object() ||
        result.value("state", json{}) != "applied")
      input_result_ = "failed";
    it = input_requests_.erase(it);
  }
  return input_requests_.empty() ? input_result_ : "pending";
}
} // namespace crossdesk

namespace crossdesk {
uint64_t ControllerSession::Clipboard(bool write, const std::string &text) {
  namespace clip = web_clipboard;
  if (!Connected() || !control_ || !clipboard_ready_ || remote_results_.size() >= 512 ||
      remote_sequence_ >= UINT32_MAX || (write && !text.empty() && !clip::ValidText(text)))
    return 0;
  const auto id = uint32_t(++remote_sequence_);
  remote_results_[id] = nullptr;
  const auto send = [this](const std::string &frame) { return Send(clip::kStream, frame); };
  const bool sent = write && !text.empty() ? clip::SendText(text, id, 16 * 1024, send)
      : send(clip::Frame(write ? clip::kClear : clip::kRead, id));
  if (!sent) { remote_results_.erase(id); return 0; }
  return id;
}
} // namespace crossdesk

namespace crossdesk {
nlohmann::json ControllerSession::Files(const nlohmann::json &request) {
  if (!Connected()) throw std::invalid_argument("NOT_READY");
  if (!files_) throw std::invalid_argument("FILES_UNSUPPORTED");
  return files_->Command(request);
}
} // namespace crossdesk
