#ifndef CROSSDESK_GUI_KEYBOARD_CONTROLLER_H_
#define CROSSDESK_GUI_KEYBOARD_CONTROLLER_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <remote_action.h>

#include "device_controller.h"

namespace crossdesk {

class GuiRuntime;

// Synchronizes local and remote keyboard state, including heartbeat recovery
// for keys whose key-up event was lost during a connection interruption.
class KeyboardController {
public:
  explicit KeyboardController(GuiRuntime &owner);

  int SendKeyCommand(int key_code, bool is_down, uint32_t scan_code = 0,
                     bool extended = false);
  void ForceReleasePressedKeys();
  void SendHeartbeat(bool force);
  void ApplyRemoteEvent(const std::string &remote_id,
                        const RemoteAction &remote_action);
  void ApplyRemoteState(const std::string &remote_id,
                        const RemoteAction &remote_action);
  void ReleaseRemotePressedKeys(const std::string &remote_id,
                                const char *reason);
  void CheckRemoteTimeouts();

private:
  struct PressedKey {
    int key_code = 0;
    uint32_t scan_code = 0;
    bool extended = false;
  };

  struct RemoteState {
    std::unordered_map<int, PressedKey> pressed_keys;
    uint32_t last_seq = 0;
    uint32_t last_seen_tick = 0;
    bool keyboard_state_seen = false;
  };

  void TrackPressedKey(int key_code, bool is_down, uint32_t scan_code,
                       bool extended);
  bool InjectRemoteKey(int key_code, bool is_down, uint32_t scan_code,
                       bool extended);

  GuiRuntime &owner_;
  std::unordered_map<int, PressedKey> pressed_keys_;
  std::mutex pressed_keys_mutex_;
  uint32_t state_sequence_ = 0;
  uint32_t last_heartbeat_tick_ = 0;
  std::unordered_map<std::string, RemoteState> remote_states_;
  std::mutex remote_states_mutex_;
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_KEYBOARD_CONTROLLER_H_
