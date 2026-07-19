#ifndef CROSSDESK_GUI_CLIPBOARD_CONTROLLER_H_
#define CROSSDESK_GUI_CLIPBOARD_CONTROLLER_H_

#include <SDL3/SDL.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace crossdesk {

class GuiRuntime;

// Owns clipboard synchronization state and keeps feedback-loop prevention out
// of the main UI coordinator.
class ClipboardController {
public:
  explicit ClipboardController(GuiRuntime &owner);

  void SetEventType(uint32_t event_type);
  uint32_t event_type() const;

  void Initialize();
  void Shutdown();
  void QueueRemoteText(const char *data, size_t size);
  void ApplyPendingRemoteText();
  void HandleLocalUpdate();

private:
  int SendToPeers(const std::string &text);

  GuiRuntime &owner_;
  uint32_t event_type_ = 0;
  std::atomic<bool> events_enabled_{false};
  std::mutex pending_mutex_;
  std::optional<std::string> pending_remote_text_;
  std::string last_text_;
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_CLIPBOARD_CONTROLLER_H_
