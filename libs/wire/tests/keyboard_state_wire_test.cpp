#include <iostream>
#include <string>

#include <remote_action.h>

namespace {

bool ExpectEqual(const char* name, size_t actual, size_t expected) {
  if (actual == expected) {
    return true;
  }

  std::cerr << name << " mismatch\n"
            << "  expected: " << expected << "\n"
            << "  actual:   " << actual << "\n";
  return false;
}

bool ExpectTrue(const char* name, bool value) {
  if (value) {
    return true;
  }

  std::cerr << name << " expected true\n";
  return false;
}

}  // namespace

int main() {
  bool ok = true;
  ok &= ExpectEqual("mouse type", crossdesk::ControlType::mouse, 0);
  ok &= ExpectEqual("keyboard type", crossdesk::ControlType::keyboard, 1);
  ok &= ExpectEqual("audio_capture type", crossdesk::ControlType::audio_capture,
                    2);
  ok &= ExpectEqual("host_infomation type",
                    crossdesk::ControlType::host_infomation, 3);
  ok &= ExpectEqual("display_id type", crossdesk::ControlType::display_id, 4);
  ok &= ExpectEqual("service_status type",
                    crossdesk::ControlType::service_status, 5);
  ok &= ExpectEqual("service_command type",
                    crossdesk::ControlType::service_command, 6);
  ok &= ExpectEqual("keyboard_state type",
                    crossdesk::ControlType::keyboard_state, 7);
  ok &= ExpectEqual("cursor_state type", crossdesk::ControlType::cursor_state,
                    8);

  crossdesk::RemoteAction action{};
  action.type = crossdesk::ControlType::keyboard_state;
  action.ks.seq = 42;
  action.ks.pressed_count = 2;
  action.ks.pressed_keys[0] = {65, 30, false};
  action.ks.pressed_keys[1] = {0xA3, 29, true};

  const std::string json = action.to_json();

  crossdesk::RemoteAction parsed{};
  ok &= ExpectTrue("parse keyboard_state", parsed.from_json(json));
  ok &= ExpectEqual("parsed type", parsed.type,
                    crossdesk::ControlType::keyboard_state);
  ok &= ExpectEqual("parsed seq", parsed.ks.seq, 42);
  ok &= ExpectEqual("parsed pressed_count", parsed.ks.pressed_count, 2);
  ok &= ExpectEqual("parsed key 0", parsed.ks.pressed_keys[0].key_value, 65);
  ok &= ExpectEqual("parsed scan 0", parsed.ks.pressed_keys[0].scan_code, 30);
  ok &= ExpectTrue("parsed extended 0", !parsed.ks.pressed_keys[0].extended);
  ok &= ExpectEqual("parsed key 1", parsed.ks.pressed_keys[1].key_value, 0xA3);
  ok &= ExpectEqual("parsed scan 1", parsed.ks.pressed_keys[1].scan_code, 29);
  ok &= ExpectTrue("parsed extended 1", parsed.ks.pressed_keys[1].extended);

  return ok ? 0 : 1;
}
