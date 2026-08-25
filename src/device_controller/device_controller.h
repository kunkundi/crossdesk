/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_CONTROLLER_H_
#define _DEVICE_CONTROLLER_H_

#include <stdio.h>

#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>

#include "display_info.h"
using json = nlohmann::json;

namespace crossdesk {

typedef enum {
  mouse = 0,
  keyboard = 1,
  audio_capture = 2,
  host_infomation = 3,
  display_id = 4,
  service_status = 5,
  service_command = 6,
  keyboard_state = 7,
  cursor_state = 8,
} ControlType;
typedef enum {
  move = 0,
  left_down,
  left_up,
  right_down,
  right_up,
  middle_down,
  middle_up,
  wheel_vertical,
  wheel_horizontal
} MouseFlag;
typedef enum { key_down = 0, key_up } KeyFlag;
typedef enum { send_sas = 0, lock_workstation } ServiceCommandFlag;
typedef struct {
  float x;
  float y;
  int s;
  MouseFlag flag;
} Mouse;

typedef struct {
  size_t key_value;
  uint32_t scan_code;
  bool extended;
  KeyFlag flag;
} Key;

inline constexpr size_t kMaxKeyboardStateKeys = 32;

typedef struct {
  size_t key_value;
  uint32_t scan_code;
  bool extended;
} KeyboardStateKey;

typedef struct {
  uint32_t seq;
  size_t pressed_count;
  KeyboardStateKey pressed_keys[kMaxKeyboardStateKeys];
} KeyboardState;

// Keep these values aligned with Slint's MouseCursor enum. The wire protocol
// intentionally carries a semantic cursor instead of a platform handle so a
// Windows, macOS or Linux host can control a different desktop platform.
enum class CursorShape : uint8_t {
  default_cursor = 0,
  none,
  help,
  pointer,
  progress,
  wait,
  crosshair,
  text,
  alias,
  copy,
  move,
  no_drop,
  not_allowed,
  grab,
  grabbing,
  col_resize,
  row_resize,
  n_resize,
  e_resize,
  s_resize,
  w_resize,
  ne_resize,
  nw_resize,
  se_resize,
  sw_resize,
  ew_resize,
  ns_resize,
  nesw_resize,
  nwse_resize,
};

typedef struct {
  uint32_t seq;
  bool visible;
  CursorShape shape;
} CursorState;

typedef struct {
  char host_name[64];
  size_t host_name_size;
  char** display_list;
  size_t display_num;
  int* left;
  int* top;
  int* right;
  int* bottom;
} HostInfo;

typedef struct {
  bool available;
  char interactive_stage[32];
} ServiceStatus;

typedef struct {
  ServiceCommandFlag flag;
} ServiceCommand;

struct RemoteAction {
  ControlType type;
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

  // parse
  std::string to_json() const { return ToJson(*this); }

  bool from_json(const std::string& json_str) {
    RemoteAction temp;
    if (!FromJson(json_str, temp)) return false;
    *this = temp;
    return true;
  }

  static std::string ToJson(const RemoteAction& a) {
    json j;
    j["type"] = a.type;
    switch (a.type) {
      case ControlType::mouse:
        j["mouse"] = {
            {"x", a.m.x}, {"y", a.m.y}, {"s", a.m.s}, {"flag", a.m.flag}};
        break;
      case ControlType::keyboard:
        j["keyboard"] = {{"key_value", a.k.key_value},
                         {"scan_code", a.k.scan_code},
                         {"extended", a.k.extended},
                         {"flag", a.k.flag}};
        break;
      case ControlType::keyboard_state: {
        json keys = json::array();
        const size_t pressed_count =
            a.ks.pressed_count < kMaxKeyboardStateKeys
                ? a.ks.pressed_count
                : kMaxKeyboardStateKeys;
        for (size_t idx = 0; idx < pressed_count; ++idx) {
          keys.push_back({{"key_value", a.ks.pressed_keys[idx].key_value},
                          {"scan_code", a.ks.pressed_keys[idx].scan_code},
                          {"extended", a.ks.pressed_keys[idx].extended}});
        }
        j["keyboard_state"] = {{"seq", a.ks.seq}, {"pressed_keys", keys}};
        break;
      }
      case ControlType::cursor_state:
        j["cursor_state"] = {{"seq", a.cs.seq},
                             {"visible", a.cs.visible},
                             {"shape", static_cast<int>(a.cs.shape)}};
        break;
      case ControlType::audio_capture:
        j["audio_capture"] = a.a;
        break;
      case ControlType::display_id:
        j["display_id"] = a.d;
        break;
      case ControlType::service_status:
        j["service_status"] = {{"available", a.ss.available},
                               {"interactive_stage", a.ss.interactive_stage}};
        break;
      case ControlType::service_command:
        j["service_command"] = {{"flag", a.c.flag}};
        break;
      case ControlType::host_infomation: {
        json displays = json::array();
        for (size_t idx = 0; idx < a.i.display_num; idx++) {
          displays.push_back(
              {{"name", a.i.display_list ? a.i.display_list[idx] : ""},
               {"left", a.i.left ? a.i.left[idx] : 0},
               {"top", a.i.top ? a.i.top[idx] : 0},
               {"right", a.i.right ? a.i.right[idx] : 0},
               {"bottom", a.i.bottom ? a.i.bottom[idx] : 0}});
        }

        j["host_info"] = {{"host_name", a.i.host_name},
                          {"display_num", a.i.display_num},
                          {"displays", displays}};
        break;
      }
    }
    return j.dump();
  }

  static bool FromJson(const std::string& json_str, RemoteAction& out) {
    try {
      json j = json::parse(json_str);
      out.type = (ControlType)j.at("type").get<int>();
      switch (out.type) {
        case ControlType::mouse:
          out.m.x = j.at("mouse").at("x").get<float>();
          out.m.y = j.at("mouse").at("y").get<float>();
          out.m.s = j.at("mouse").at("s").get<int>();
          out.m.flag = (MouseFlag)j.at("mouse").at("flag").get<int>();
          break;
        case ControlType::keyboard:
          out.k.key_value = j.at("keyboard").at("key_value").get<size_t>();
          out.k.scan_code =
              j.at("keyboard").value("scan_code", static_cast<uint32_t>(0));
          out.k.extended = j.at("keyboard").value("extended", false);
          out.k.flag = (KeyFlag)j.at("keyboard").at("flag").get<int>();
          break;
        case ControlType::keyboard_state: {
          const auto& keyboard_state_json = j.at("keyboard_state");
          out.ks.seq = keyboard_state_json.value("seq", 0u);
          out.ks.pressed_count = 0;

          const auto keys_json =
              keyboard_state_json.value("pressed_keys", json::array());
          if (!keys_json.is_array()) {
            break;
          }

          const size_t count =
              keys_json.size() < kMaxKeyboardStateKeys
                  ? keys_json.size()
                  : kMaxKeyboardStateKeys;
          for (size_t idx = 0; idx < count; ++idx) {
            const auto& key_json = keys_json[idx];
            out.ks.pressed_keys[idx].key_value =
                key_json.at("key_value").get<size_t>();
            out.ks.pressed_keys[idx].scan_code =
                key_json.value("scan_code", static_cast<uint32_t>(0));
            out.ks.pressed_keys[idx].extended =
                key_json.value("extended", false);
          }
          out.ks.pressed_count = count;
          break;
        }
        case ControlType::cursor_state: {
          const auto& cursor_state_json = j.at("cursor_state");
          const int shape = cursor_state_json.at("shape").get<int>();
          if (shape < static_cast<int>(CursorShape::default_cursor) ||
              shape > static_cast<int>(CursorShape::nwse_resize)) {
            return false;
          }
          out.cs.seq = cursor_state_json.at("seq").get<uint32_t>();
          out.cs.visible = cursor_state_json.at("visible").get<bool>();
          out.cs.shape = static_cast<CursorShape>(shape);
          break;
        }
        case ControlType::audio_capture:
          out.a = j.at("audio_capture").get<bool>();
          break;
        case ControlType::display_id:
          out.d = j.at("display_id").get<int>();
          break;
        case ControlType::service_status: {
          const auto& service_status_json = j.at("service_status");
          out.ss.available = service_status_json.value("available", false);
          std::string interactive_stage =
              service_status_json.value("interactive_stage", std::string());
          std::strncpy(out.ss.interactive_stage, interactive_stage.c_str(),
                       sizeof(out.ss.interactive_stage) - 1);
          out.ss.interactive_stage[sizeof(out.ss.interactive_stage) - 1] = '\0';
          break;
        }
        case ControlType::service_command:
          out.c.flag = static_cast<ServiceCommandFlag>(
              j.at("service_command").at("flag").get<int>());
          break;
        case ControlType::host_infomation: {
          std::string host_name =
              j.at("host_info").at("host_name").get<std::string>();
          strncpy(out.i.host_name, host_name.c_str(), sizeof(out.i.host_name));
          out.i.host_name[sizeof(out.i.host_name) - 1] = '\0';
          out.i.host_name_size = host_name.size();

          out.i.display_num = j.at("host_info").at("display_num").get<size_t>();
          auto displays = j.at("host_info").at("displays");

          out.i.display_list =
              (char**)malloc(out.i.display_num * sizeof(char*));
          out.i.left = (int*)malloc(out.i.display_num * sizeof(int));
          out.i.top = (int*)malloc(out.i.display_num * sizeof(int));
          out.i.right = (int*)malloc(out.i.display_num * sizeof(int));
          out.i.bottom = (int*)malloc(out.i.display_num * sizeof(int));

          for (size_t idx = 0; idx < out.i.display_num; idx++) {
            std::string name = displays[idx].at("name").get<std::string>();
            out.i.display_list[idx] = (char*)malloc(name.size() + 1);
            strcpy(out.i.display_list[idx], name.c_str());
            out.i.left[idx] = displays[idx].at("left").get<int>();
            out.i.top[idx] = displays[idx].at("top").get<int>();
            out.i.right[idx] = displays[idx].at("right").get<int>();
            out.i.bottom[idx] = displays[idx].at("bottom").get<int>();
          }
          break;
        }
      }
      return true;
    } catch (const std::exception& e) {
      printf("Failed to parse RemoteAction JSON: %s\n", e.what());
      return false;
    }
  }
};

// int key_code, bool is_down, uint32_t scan_code, bool extended
typedef void (*OnKeyAction)(int, bool, uint32_t, bool, void*);

class DeviceController {
 public:
  virtual ~DeviceController() {}

 public:
  // virtual int Init(int screen_width, int screen_height);
  // virtual int Destroy();
  // virtual int SendMouseCommand(RemoteAction remote_action);

  // virtual int Hook();
  // virtual int Unhook();
};
}  // namespace crossdesk
#endif
