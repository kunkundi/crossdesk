/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_CONTROLLER_H_
#define _DEVICE_CONTROLLER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "remote_cursor_shape.h"

namespace crossdesk {

enum ControlType {
  invalid = -1,
  mouse = 0,
  keyboard = 1,
  audio_capture = 2,
  host_infomation = 3,
  display_id = 4,
  service_status = 5,
  service_command = 6,
  keyboard_state = 7,
  cursor_state = 8,
};

enum MouseFlag {
  move = 0,
  left_down,
  left_up,
  right_down,
  right_up,
  middle_down,
  middle_up,
  wheel_vertical,
  wheel_horizontal,
};

enum KeyFlag { key_down = 0, key_up };
enum ServiceCommandFlag { send_sas = 0, lock_workstation };

struct Mouse {
  float x;
  float y;
  int s;
  MouseFlag flag;
};

struct Key {
  std::size_t key_value;
  uint32_t scan_code;
  bool extended;
  KeyFlag flag;
};

inline constexpr std::size_t kMaxKeyboardStateKeys = 32;

struct KeyboardStateKey {
  std::size_t key_value;
  uint32_t scan_code;
  bool extended;
};

struct KeyboardState {
  uint32_t seq;
  std::size_t pressed_count;
  KeyboardStateKey pressed_keys[kMaxKeyboardStateKeys];
};

struct CursorState {
  uint32_t seq;
  bool visible;
  RemoteCursorShape shape;
  bool position_valid;
  float x;
  float y;
  // Normalized displacement from the input hotspot to the visible cursor
  // anchor. This is presentation metadata and must not affect input mapping.
  float visual_offset_x;
  float visual_offset_y;
  int display_id;
  // Whether receivers should apply the position fields in this message.
  // Shape-only updates set this to false so cursor appearance can remain
  // responsive while position feedback to the input source is suppressed.
  bool position_update;
};

struct HostInfo {
  char host_name[64];
  std::size_t host_name_size;
  char** display_list;
  std::size_t display_num;
  int* left;
  int* top;
  int* right;
  int* bottom;
};

struct ServiceStatus {
  bool available;
  char interactive_stage[32];
};

struct ServiceCommand {
  ServiceCommandFlag flag;
};

struct RemoteAction {
  ControlType type = ControlType::invalid;
  union {
    Mouse m;
    Key k;
    KeyboardState ks;
    CursorState cs;
    HostInfo i;
    bool a;
    int d;
    ServiceStatus ss;
    ServiceCommand c;
  };

  std::string to_json() const;
  bool from_json(const std::string& json_string);

  static std::string ToJson(const RemoteAction& action);
  static bool FromJson(const std::string& json_string, RemoteAction& output);
};

// Releases the dynamically allocated display arrays held by host information.
// Other RemoteAction variants do not own memory and are left unchanged.
void FreeRemoteAction(RemoteAction& action);

// int key_code, bool is_down, uint32_t scan_code, bool extended
using OnKeyAction = void (*)(int, bool, uint32_t, bool, void*);

class DeviceController {
 public:
  virtual ~DeviceController() = default;
};

}  // namespace crossdesk

#endif
