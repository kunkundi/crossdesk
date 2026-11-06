#include "runtime/gui_runtime.h"
#include <stream_names.h>

namespace crossdesk {
void GuiRuntime::HandleAutomation(const std::string& remote_id,
                                  const nlohmann::json& request) {
  using nlohmann::json;
  if (request.value("version", json{}) != 1 || !request.contains("id") ||
      !request["id"].is_number_unsigned() || request["id"].get<uint64_t>() == 0)
    return;
  const auto source = ControllerEvents(remote_id);
  if (!source || !source->DataAllowed()) return;
  const auto id = request["id"].get<uint64_t>();
  const auto reply = [this, remote_id, id, source](bool ok, json result) {
    const auto current = ControllerEvents(remote_id);
    if (current != source || !current->DataAllowed()) return;
    const auto message = json{{"type", "automation_result"}, {"version", 1},
        {"id", id}, {"ok", ok}, {ok ? "result" : "error", std::move(result)}}.dump();
    SendControllerData(remote_id, message.data(), message.size(), kControlStream);
  };
  try {
    const auto method = request.value("method", std::string{});
    if (method == "capabilities") {
      reply(true, {{"input_ack", true}});
    } else if (method == "input") {
      RemoteAction action{};
      if (!request.contains("arguments") ||
          !action.from_json(request["arguments"].dump())) {
        reply(false, "INVALID_ARGUMENT");
        return;
      }
      const auto result = remote_input_.Push(remote_id, action,
          [reply](InputOutcome outcome) {
            reply(true, {{"state", InputOutcomeName(outcome)}});
          });
      FreeRemoteAction(action);
      if (result == RemoteInputDispatcher::Result::rejected ||
          result == RemoteInputDispatcher::Result::overflow)
        reply(false, "INPUT_REJECTED");
    } else {
      reply(false, "UNSUPPORTED_OPERATION");
    }
  } catch (...) {
    reply(false, "INVALID_ARGUMENT");
  }
}
} // namespace crossdesk
