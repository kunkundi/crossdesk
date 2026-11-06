#include "runtime/gui_runtime.h"
#include <stream_names.h>
#include "screen_lock.h"

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
      json capabilities{{"input_ack", true}, {"clipboard", true}, {"files", true}};
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
      capabilities["system"] = true;
      capabilities["desktop"] = true;
#endif
      reply(true, std::move(capabilities));
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
    } else if (method == "desktop") {
      const auto arguments = request.at("arguments");
      if (!arguments.is_object()) { reply(false, "INVALID_ARGUMENT"); return; }
      std::lock_guard lock(automation_mutex_);
      if (automation_closing_ || automation_tasks_.size() >= 32) { reply(false, "BUSY"); return; }
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      automation_tasks_.push_back([this, source, remote_id, arguments, reply, deadline] {
        if (std::chrono::steady_clock::now() > deadline) { reply(false, "REQUEST_EXPIRED"); return; }
        if (ControllerEvents(remote_id) != source || !IsAuthorizedController(remote_id)) return;
        try { reply(true, desktop_automation_.Execute(remote_id, arguments)); }
        catch (const std::exception& error) {
          const std::string code = error.what();
          reply(false, !code.empty() && code.size() <= 64 && code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos
              ? code : "UI_AUTOMATION_FAILED");
        }
      });
    } else if (method == "system") {
      const auto operation = request.at("arguments").at("operation").get<std::string>();
      if (operation != "status" && operation != "lock" && operation != "send_sas") {
        reply(false, "INVALID_ARGUMENT");
        return;
      }
      std::lock_guard lock(automation_mutex_);
      if (automation_closing_ || automation_tasks_.size() >= 32) { reply(false, "BUSY"); return; }
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      automation_tasks_.push_back([this, source, remote_id, operation, reply, deadline] {
        if (std::chrono::steady_clock::now() > deadline) { reply(false, "REQUEST_EXPIRED"); return; }
        if (ControllerEvents(remote_id) != source || !IsAuthorizedController(remote_id)) return;
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__OHOS__))
        if (operation == "status") {
          const auto state = QueryScreenLock();
          json result{{"lock_state", state == ScreenLockState::locked ? "locked"
              : state == ScreenLockState::unlocked ? "unlocked" : "unknown"}};
#ifdef _WIN32
          result["interactive_stage"] = WindowsInputStage();
          result["service_available"] = local_service_available_.load();
          result["send_sas_available"] = local_service_available_.load();
#else
          result["send_sas_available"] = false;
#endif
          reply(true, std::move(result));
        } else if (operation == "lock") {
          reply(true, {{"accepted", LockScreen()}});
        } else {
#ifdef _WIN32
          if (!local_service_available_.load()) { reply(false, "SERVICE_UNAVAILABLE"); return; }
          pending_windows_service_sas_.store(true);
          reply(true, {{"accepted", true}, {"state", "queued"}});
#else
          reply(false, "UNSUPPORTED_OPERATION");
#endif
        }
#else
        reply(false, "UNSUPPORTED_OPERATION");
#endif
      });
    } else {
      reply(false, "UNSUPPORTED_OPERATION");
    }
  } catch (...) {
    reply(false, "INVALID_ARGUMENT");
  }
}

void GuiRuntime::ProcessAutomationRequests() {
  std::function<void()> task;
  {
    std::lock_guard lock(automation_mutex_);
    if (automation_tasks_.empty() || automation_closing_) return;
    task = std::move(automation_tasks_.front());
    automation_tasks_.pop_front();
  }
  try { task(); } catch (...) {}
}
} // namespace crossdesk
