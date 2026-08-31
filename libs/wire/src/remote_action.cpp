#include <remote_action.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include <nlohmann/json.hpp>

namespace crossdesk {
namespace {

using json = nlohmann::json;

void ResetHostInfo(HostInfo& info) {
  info.host_name[0] = '\0';
  info.host_name_size = 0;
  info.display_list = nullptr;
  info.display_num = 0;
  info.left = nullptr;
  info.top = nullptr;
  info.right = nullptr;
  info.bottom = nullptr;
}

bool AllocateHostDisplays(HostInfo& info, std::size_t count) {
  if (count == 0) return true;

  info.display_list = static_cast<char**>(std::calloc(count, sizeof(char*)));
  info.left = static_cast<int*>(std::malloc(count * sizeof(int)));
  info.top = static_cast<int*>(std::malloc(count * sizeof(int)));
  info.right = static_cast<int*>(std::malloc(count * sizeof(int)));
  info.bottom = static_cast<int*>(std::malloc(count * sizeof(int)));
  return info.display_list && info.left && info.top && info.right &&
         info.bottom;
}

}  // namespace

std::string RemoteAction::to_json() const { return ToJson(*this); }

bool RemoteAction::from_json(const std::string& json_string) {
  RemoteAction temporary{};
  if (!FromJson(json_string, temporary)) return false;
  *this = temporary;
  return true;
}

std::string RemoteAction::ToJson(const RemoteAction& action) {
  if (action.type == ControlType::invalid) return {};

  json object;
  object["type"] = action.type;
  switch (action.type) {
    case ControlType::mouse:
      object["mouse"] = {{"x", action.m.x},
                         {"y", action.m.y},
                         {"s", action.m.s},
                         {"flag", action.m.flag}};
      break;
    case ControlType::keyboard:
      object["keyboard"] = {{"key_value", action.k.key_value},
                            {"scan_code", action.k.scan_code},
                            {"extended", action.k.extended},
                            {"flag", action.k.flag}};
      break;
    case ControlType::keyboard_state: {
      json keys = json::array();
      const std::size_t pressed_count =
          std::min(action.ks.pressed_count, kMaxKeyboardStateKeys);
      for (std::size_t index = 0; index < pressed_count; ++index) {
        keys.push_back(
            {{"key_value", action.ks.pressed_keys[index].key_value},
             {"scan_code", action.ks.pressed_keys[index].scan_code},
             {"extended", action.ks.pressed_keys[index].extended}});
      }
      object["keyboard_state"] =
          {{"seq", action.ks.seq}, {"pressed_keys", keys}};
      break;
    }
    case ControlType::cursor_state:
      object["cursor_state"] =
          {{"seq", action.cs.seq},
           {"visible", action.cs.visible},
           {"shape", static_cast<int>(action.cs.shape)},
           {"position_valid", action.cs.position_valid},
           {"x", action.cs.x},
           {"y", action.cs.y},
           {"visual_offset_x", action.cs.visual_offset_x},
           {"visual_offset_y", action.cs.visual_offset_y},
           {"display_id", action.cs.display_id},
           {"position_update", action.cs.position_update}};
      break;
    case ControlType::audio_capture:
      object["audio_capture"] = action.a;
      break;
    case ControlType::display_id:
      object["display_id"] = action.d;
      break;
    case ControlType::service_status:
      object["service_status"] =
          {{"available", action.ss.available},
           {"interactive_stage", action.ss.interactive_stage}};
      break;
    case ControlType::service_command:
      object["service_command"] = {{"flag", action.c.flag}};
      break;
    case ControlType::host_infomation: {
      json displays = json::array();
      for (std::size_t index = 0; index < action.i.display_num; ++index) {
        displays.push_back(
            {{"name", action.i.display_list ? action.i.display_list[index]
                                             : ""},
             {"left", action.i.left ? action.i.left[index] : 0},
             {"top", action.i.top ? action.i.top[index] : 0},
             {"right", action.i.right ? action.i.right[index] : 0},
             {"bottom", action.i.bottom ? action.i.bottom[index] : 0}});
      }
      object["host_info"] = {{"host_name", action.i.host_name},
                             {"display_num", action.i.display_num},
                             {"displays", displays}};
      break;
    }
    case ControlType::invalid:
    default:
      return {};
  }
  return object.dump();
}

bool RemoteAction::FromJson(const std::string& json_string,
                            RemoteAction& output) {
  bool owns_host_info = false;
  try {
    const json object = json::parse(json_string);
    output.type = static_cast<ControlType>(object.at("type").get<int>());
    switch (output.type) {
      case ControlType::mouse:
        output.m.x = object.at("mouse").at("x").get<float>();
        output.m.y = object.at("mouse").at("y").get<float>();
        output.m.s = object.at("mouse").at("s").get<int>();
        output.m.flag = static_cast<MouseFlag>(
            object.at("mouse").at("flag").get<int>());
        break;
      case ControlType::keyboard:
        output.k.key_value =
            object.at("keyboard").at("key_value").get<std::size_t>();
        output.k.scan_code = object.at("keyboard").value("scan_code", 0u);
        output.k.extended = object.at("keyboard").value("extended", false);
        output.k.flag = static_cast<KeyFlag>(
            object.at("keyboard").at("flag").get<int>());
        break;
      case ControlType::keyboard_state: {
        const auto& keyboard_state_object = object.at("keyboard_state");
        output.ks.seq = keyboard_state_object.value("seq", 0u);
        output.ks.pressed_count = 0;
        const auto keys =
            keyboard_state_object.value("pressed_keys", json::array());
        if (!keys.is_array()) break;
        const std::size_t count =
            std::min(keys.size(), kMaxKeyboardStateKeys);
        for (std::size_t index = 0; index < count; ++index) {
          output.ks.pressed_keys[index].key_value =
              keys[index].at("key_value").get<std::size_t>();
          output.ks.pressed_keys[index].scan_code =
              keys[index].value("scan_code", 0u);
          output.ks.pressed_keys[index].extended =
              keys[index].value("extended", false);
        }
        output.ks.pressed_count = count;
        break;
      }
      case ControlType::cursor_state: {
        const auto& cursor_state_object = object.at("cursor_state");
        const int shape = cursor_state_object.at("shape").get<int>();
        if (shape < static_cast<int>(RemoteCursorShape::default_cursor) ||
            shape > static_cast<int>(RemoteCursorShape::nwse_resize)) {
          return false;
        }
        output.cs.seq = cursor_state_object.at("seq").get<uint32_t>();
        output.cs.visible = cursor_state_object.at("visible").get<bool>();
        output.cs.shape = static_cast<RemoteCursorShape>(shape);
        output.cs.position_valid =
            cursor_state_object.value("position_valid", false);
        output.cs.x = cursor_state_object.value("x", 0.5f);
        output.cs.y = cursor_state_object.value("y", 0.5f);
        output.cs.visual_offset_x =
            cursor_state_object.value("visual_offset_x", 0.0f);
        output.cs.visual_offset_y =
            cursor_state_object.value("visual_offset_y", 0.0f);
        output.cs.display_id = cursor_state_object.value("display_id", -1);
        // Cursor state messages predating position-only echo suppression
        // always carried an authoritative position update.
        output.cs.position_update =
            cursor_state_object.value("position_update", true);
        if (!std::isfinite(output.cs.x) || !std::isfinite(output.cs.y) ||
            !std::isfinite(output.cs.visual_offset_x) ||
            !std::isfinite(output.cs.visual_offset_y)) {
          return false;
        }
        output.cs.x = std::clamp(output.cs.x, 0.0f, 1.0f);
        output.cs.y = std::clamp(output.cs.y, 0.0f, 1.0f);
        output.cs.visual_offset_x =
            std::clamp(output.cs.visual_offset_x, -1.0f, 1.0f);
        output.cs.visual_offset_y =
            std::clamp(output.cs.visual_offset_y, -1.0f, 1.0f);
        break;
      }
      case ControlType::audio_capture:
        output.a = object.at("audio_capture").get<bool>();
        break;
      case ControlType::display_id:
        output.d = object.at("display_id").get<int>();
        break;
      case ControlType::service_status: {
        const auto& service_status_object = object.at("service_status");
        output.ss.available = service_status_object.value("available", false);
        const std::string stage = service_status_object.value(
            "interactive_stage", std::string());
        std::strncpy(output.ss.interactive_stage, stage.c_str(),
                     sizeof(output.ss.interactive_stage) - 1);
        output.ss.interactive_stage[sizeof(output.ss.interactive_stage) - 1] =
            '\0';
        break;
      }
      case ControlType::service_command:
        output.c.flag = static_cast<ServiceCommandFlag>(
            object.at("service_command").at("flag").get<int>());
        break;
      case ControlType::host_infomation: {
        ResetHostInfo(output.i);
        owns_host_info = true;
        const auto& host_info_object = object.at("host_info");
        const std::string host_name =
            host_info_object.at("host_name").get<std::string>();
        std::strncpy(output.i.host_name, host_name.c_str(),
                     sizeof(output.i.host_name) - 1);
        output.i.host_name[sizeof(output.i.host_name) - 1] = '\0';
        output.i.host_name_size = std::strlen(output.i.host_name);

        const auto& displays = host_info_object.at("displays");
        if (!displays.is_array()) return false;
        output.i.display_num =
            host_info_object.at("display_num").get<std::size_t>();
        if (output.i.display_num != displays.size()) return false;
        if (!AllocateHostDisplays(output.i, output.i.display_num)) {
          FreeRemoteAction(output);
          return false;
        }

        for (std::size_t index = 0; index < output.i.display_num; ++index) {
          const std::string name =
              displays[index].at("name").get<std::string>();
          output.i.display_list[index] =
              static_cast<char*>(std::malloc(name.size() + 1));
          if (!output.i.display_list[index]) {
            FreeRemoteAction(output);
            return false;
          }
          std::memcpy(output.i.display_list[index], name.c_str(),
                      name.size() + 1);
          output.i.left[index] = displays[index].at("left").get<int>();
          output.i.top[index] = displays[index].at("top").get<int>();
          output.i.right[index] = displays[index].at("right").get<int>();
          output.i.bottom[index] = displays[index].at("bottom").get<int>();
        }
        break;
      }
      default:
        return false;
    }
    return true;
  } catch (const std::exception& exception) {
    if (owns_host_info) {
      FreeRemoteAction(output);
    }
    std::fprintf(stderr, "Failed to parse RemoteAction JSON: %s\n",
                 exception.what());
    return false;
  }
}

void FreeRemoteAction(RemoteAction& action) {
  if (action.type != ControlType::host_infomation) return;

  if (action.i.display_list) {
    for (std::size_t index = 0; index < action.i.display_num; ++index) {
      std::free(action.i.display_list[index]);
    }
  }
  std::free(action.i.display_list);
  std::free(action.i.left);
  std::free(action.i.top);
  std::free(action.i.right);
  std::free(action.i.bottom);
  ResetHostInfo(action.i);
}

}  // namespace crossdesk
