/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-01
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _REMOTE_ACTION_H_
#define _REMOTE_ACTION_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <remote_cursor_shape.h>

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
  privacy_command = 9,
  privacy_status = 10,
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

// Optional presentation metadata. Zero preserves pre-metadata peers' behavior.
enum class CursorRenderMode : uint8_t {
  legacy = 0,
  separate,
  embedded,
  hidden,
  unknown,
};

enum class CursorHiddenReason : uint8_t {
  unspecified = 0,
  system_hidden,
  system_suppressed,
  no_pointing_device,
  sampling_failed,
  secure_desktop_pending,
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
  // Keep visible/shape for old receivers; new receivers can distinguish an
  // embedded pointer from a hidden pointer and an unavailable sample.
  CursorRenderMode render_mode;
  CursorHiddenReason hidden_reason;
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
  // Optional capability; absent in legacy desktop and web host information.
  bool supports_privacy_screen;
};

struct ServiceStatus {
  bool available;
  char interactive_stage[32];
};

struct ServiceCommand {
  ServiceCommandFlag flag;
};

enum class PrivacyState { unsupported, off, starting, on, stopping, failed };
enum class PrivacyCommandFlag { query, enable, disable };
struct PrivacyCommand {
  PrivacyCommandFlag flag;
  bool block_local_input;
};
struct PrivacyStatus {
  PrivacyState state;
  bool supported;
  bool input_block_supported;
  bool overlay_active;
  bool input_blocked;
  bool remote_paused;
  uint32_t revision;
  char reason[256];
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
    PrivacyCommand pc;
    PrivacyStatus ps;
  };

  std::string to_json() const;
  bool from_json(const std::string& json_string);

  static std::string ToJson(const RemoteAction& action);
  static bool FromJson(const std::string& json_string, RemoteAction& output);
};

// Releases the dynamically allocated display arrays held by host information.
// Other RemoteAction variants do not own memory and are left unchanged.
void FreeRemoteAction(RemoteAction& action);

// One display as advertised in host information; bounds are virtual-desktop
// pixel coordinates.
struct HostDisplay {
  std::string name;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

// Builds the host_infomation message advertised on every new connection.
// |host_name| is truncated to the wire field size. The display arrays are
// heap-allocated with the same layout the decoder produces; release the
// result with FreeRemoteAction. An allocation failure yields an empty display
// list rather than a partially filled one.
RemoteAction MakeHostInformation(const std::string& host_name,
                                 const std::vector<HostDisplay>& displays,
                                 bool supports_privacy_screen);

}  // namespace crossdesk

#endif
