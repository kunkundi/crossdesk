#include "automation_protocol.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace crossdesk::cli::automation {
void Fields(const json &j, std::initializer_list<const char *> names) {
  if (!j.is_object())
    throw std::invalid_argument("INVALID_REQUEST");
  for (auto it = j.begin(); it != j.end(); ++it)
    if (std::none_of(names.begin(), names.end(),
                     [&](const char *s) { return it.key() == s; }))
      throw std::invalid_argument("UNKNOWN_FIELD");
}
int Number(const json &j, const char *key, int low, int high, int fallback) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_number_integer())
    throw std::invalid_argument("INVALID_REQUEST");
  const auto n = j[key].get<int64_t>();
  if (n < low || n > high)
    throw std::invalid_argument("INVALID_REQUEST");
  return int(n);
}
uint64_t Sequence(const json &j, const char *key, uint64_t fallback) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_number_unsigned())
    throw std::invalid_argument("INVALID_REQUEST");
  return j[key].get<uint64_t>();
}
bool Flag(const json &j, const char *key, bool fallback) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_boolean())
    throw std::invalid_argument("INVALID_REQUEST");
  return j[key].get<bool>();
}
std::string String(const json &j, const char *key, size_t limit) {
  if (!j.contains(key) || !j[key].is_string())
    throw std::invalid_argument("INVALID_REQUEST");
  auto s = j[key].get<std::string>();
  if (s.empty() || s.size() > limit || s.find('\0') != std::string::npos)
    throw std::invalid_argument("INVALID_REQUEST");
  return s;
}
namespace {
int KeyCode(const std::string &key) {
  if (key.size() == 1 &&
      ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9')))
    return key[0];
  static const std::map<std::string, int> keys = {{"BACKSPACE", 8},
                                                  {"TAB", 9},
                                                  {"ENTER", 13},
                                                  {"SHIFT", 16},
                                                  {"CTRL", 17},
                                                  {"ALT", 18},
                                                  {"PAUSE", 19},
                                                  {"CAPS_LOCK", 20},
                                                  {"ESC", 27},
                                                  {"SPACE", 32},
                                                  {"PAGE_UP", 33},
                                                  {"PAGE_DOWN", 34},
                                                  {"END", 35},
                                                  {"HOME", 36},
                                                  {"LEFT", 37},
                                                  {"UP", 38},
                                                  {"RIGHT", 39},
                                                  {"DOWN", 40},
                                                  {"INSERT", 45},
                                                  {"DELETE", 46},
                                                  {"META", 91},
                                                  {"F1", 112},
                                                  {"F2", 113},
                                                  {"F3", 114},
                                                  {"F4", 115},
                                                  {"F5", 116},
                                                  {"F6", 117},
                                                  {"F7", 118},
                                                  {"F8", 119},
                                                  {"F9", 120},
                                                  {"F10", 121},
                                                  {"F11", 122},
                                                  {"F12", 123},
                                                  {"F13", 124},
                                                  {"F14", 125},
                                                  {"F15", 126},
                                                  {"F16", 127},
                                                  {"F17", 128},
                                                  {"F18", 129},
                                                  {"F19", 130},
                                                  {"F20", 131},
                                                  {"F21", 132},
                                                  {"F22", 133},
                                                  {"F23", 134},
                                                  {"F24", 135},
                                                  {"NUMPAD0", 96},
                                                  {"NUMPAD1", 97},
                                                  {"NUMPAD2", 98},
                                                  {"NUMPAD3", 99},
                                                  {"NUMPAD4", 100},
                                                  {"NUMPAD5", 101},
                                                  {"NUMPAD6", 102},
                                                  {"NUMPAD7", 103},
                                                  {"NUMPAD8", 104},
                                                  {"NUMPAD9", 105},
                                                  {"NUM_LOCK", 144},
                                                  {"SCROLL_LOCK", 145},
                                                  {"PRINT_SCREEN", 44},
                                                  {"MENU", 93},
                                                  {"MULTIPLY", 106},
                                                  {"ADD", 107},
                                                  {"SUBTRACT", 109},
                                                  {"DECIMAL", 110},
                                                  {"DIVIDE", 111},
                                                  {"VOLUME_MUTE", 173},
                                                  {"VOLUME_DOWN", 174},
                                                  {"VOLUME_UP", 175},
                                                  {"MEDIA_NEXT", 176},
                                                  {"MEDIA_PREVIOUS", 177},
                                                  {"MEDIA_STOP", 178},
                                                  {"MEDIA_PLAY_PAUSE", 179},
                                                  {"SEMICOLON", 186},
                                                  {"EQUAL", 187},
                                                  {"COMMA", 188},
                                                  {"MINUS", 189},
                                                  {"PERIOD", 190},
                                                  {"SLASH", 191},
                                                  {"BACKQUOTE", 192},
                                                  {"BRACKET_LEFT", 219},
                                                  {"BACKSLASH", 220},
                                                  {"BRACKET_RIGHT", 221},
                                                  {"QUOTE", 222}};
  auto it = keys.find(key);
  if (it == keys.end())
    throw std::invalid_argument("INVALID_KEY");
  return it->second;
}
} // namespace

std::vector<Step> BuildInputSteps(const json &a, const InputGeometry &o) {
  const auto type = String(a, "type", 32);
  std::vector<Step> steps;
  auto point = [&](const json &p, int delay, int flag = 0, int wheel = 0) {
    auto coordinate = [&](const char *key, int size, int offset, int span,
                          int source) {
      if (!p.contains(key) || !p[key].is_number())
        throw std::invalid_argument("INVALID_COORDINATE");
      const auto value = p[key].get<double>();
      if (!std::isfinite(value) || value < 0 || value >= size)
        throw std::invalid_argument("INVALID_COORDINATE");
      const auto center = std::min(value + .5, double(size) - .5);
      return float(std::clamp((offset + center * span / size) / source, 0.0, 1.0));
    };
    Step step;
    step.delay = delay;
    step.flag = flag;
    step.wheel = wheel;
    step.x =
        coordinate("x", o.width, o.region.x, o.region.width, o.source_width);
    step.y =
        coordinate("y", o.height, o.region.y, o.region.height, o.source_height);
    steps.push_back(std::move(step));
  };
  auto key = [&](int code, bool down, int delay) {
    Step step;
    step.kind = StepKind::Key;
    step.key = code;
    step.down = down;
    step.delay = delay;
    steps.push_back(std::move(step));
  };
  if (type == "mouse_down" || type == "mouse_up") {
    Fields(a, {"type", "x", "y", "button"});
    const auto button = a.contains("button") ? String(a, "button", 8) : "left";
    const int flag = button == "left" ? 1 : button == "right" ? 3 : button == "middle" ? 5 : -1;
    if (flag < 0) throw std::invalid_argument("INVALID_BUTTON");
    point(a, 0);
    point(a, 10, flag + (type == "mouse_up" ? 1 : 0));
  } else if (type == "key_down" || type == "key_up") {
    Fields(a, {"type", "key"});
    key(KeyCode(String(a, "key", 16)), type == "key_down", 0);
  } else if (type == "move" || type == "click" || type == "double_click") {
    Fields(a, type == "move"
                  ? std::initializer_list<const char *>{"type", "x", "y",
                                                        "modifiers"}
                  : std::initializer_list<const char *>{"type", "x", "y",
                                                        "button", "modifiers"});
    point(a, 0);
    if (type != "move") {
      const auto button =
          a.contains("button") ? String(a, "button", 8) : "left";
      const int flag = button == "left"     ? 1
                       : button == "right"  ? 3
                       : button == "middle" ? 5
                                            : -1;
      if (flag < 0)
        throw std::invalid_argument("INVALID_BUTTON");
      point(a, 10, flag);
      point(a, 40, flag + 1);
      if (type == "double_click") {
        point(a, 110, flag);
        point(a, 140, flag + 1);
      }
    }
  } else if (type == "scroll") {
    Fields(a, {"type", "x", "y", "direction", "ticks", "modifiers"});
    const auto direction = String(a, "direction", 8);
    if (direction != "up" && direction != "down" && direction != "left" &&
        direction != "right")
      throw std::invalid_argument("INVALID_DIRECTION");
    const int ticks = Number(a, "ticks", 1, 10, 1);
    point(a, 0);
    point(a, 10, direction == "left" || direction == "right" ? 8 : 7,
          (direction == "up" || direction == "right" ? 120 : -120) * ticks);
  } else if (type == "drag") {
    Fields(a, {"type", "path", "duration_ms", "button", "modifiers"});
    const auto button = a.contains("button") ? String(a, "button", 8) : "left";
    const int flag = button == "left"     ? 1
                     : button == "right"  ? 3
                     : button == "middle" ? 5
                                          : -1;
    if (flag < 0)
      throw std::invalid_argument("INVALID_BUTTON");
    const auto &path = a.at("path");
    if (!path.is_array() || path.size() < 2 || path.size() > 128)
      throw std::invalid_argument("INVALID_PATH");
    const int duration = Number(a, "duration_ms", 100, 30000, 500);
    for (const auto &p : path)
      Fields(p, {"x", "y"});
    point(path.front(), 0);
    point(path.front(), 10, flag);
    for (size_t i = 1; i < path.size(); ++i)
      point(path[i], 10 + int(i * duration / (path.size() - 1)));
    point(path.back(), duration + 30, flag + 1);
  } else if (type == "key_press") {
    Fields(a, {"type", "key", "modifiers", "duration_ms"});
    const auto code = KeyCode(String(a, "key", 16));
    key(code, true, 0);
    key(code, false, Number(a, "duration_ms", 10, 30000, 40));
  } else if (type == "hotkey") {
    Fields(a, {"type", "keys", "duration_ms"});
    const auto &keys = a.at("keys");
    if (!keys.is_array() || keys.size() < 2 || keys.size() > 6)
      throw std::invalid_argument("INVALID_KEYS");
    std::set<int> seen;
    std::vector<int> codes;
    for (const auto &item : keys) {
      if (!item.is_string())
        throw std::invalid_argument("INVALID_KEY");
      int code = KeyCode(item.get<std::string>());
      if (!seen.insert(code).second)
        throw std::invalid_argument("DUPLICATE_KEY");
      codes.push_back(code);
    }
    int delay = 0;
    for (int code : codes) {
      key(code, true, delay);
      delay += 15;
    }
    delay += Number(a, "duration_ms", 10, 30000, 30);
    for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
      key(*it, false, delay);
      delay += 15;
    }
  } else if (type == "type_text") {
    Fields(a, {"type", "text"});
    Step step;
    step.kind = StepKind::Text;
    step.text = String(a, "text", 16384);
    steps.push_back(std::move(step));
  } else
    throw std::invalid_argument("UNSUPPORTED_ACTION");
  if (a.contains("modifiers")) {
    const auto &modifiers = a["modifiers"];
    if (!modifiers.is_array() || modifiers.size() > 4)
      throw std::invalid_argument("INVALID_KEYS");
    std::set<int> seen;
    std::vector<Step> prefix;
    for (const auto &item : modifiers) {
      if (!item.is_string())
        throw std::invalid_argument("INVALID_KEY");
      const auto name = item.get<std::string>();
      if (name != "CTRL" && name != "ALT" && name != "SHIFT" && name != "META")
        throw std::invalid_argument("INVALID_KEY");
      const int code = KeyCode(name);
      if (!seen.insert(code).second ||
          (type == "key_press" && String(a, "key", 16) == name))
        throw std::invalid_argument("DUPLICATE_KEY");
      Step step;
      step.kind = StepKind::Key;
      step.key = code;
      step.down = true;
      step.delay = int(prefix.size()) * 15;
      prefix.push_back(step);
    }
    const int offset = int(prefix.size()) * 15;
    for (auto &step : steps)
      step.delay += offset;
    steps.insert(steps.begin(), prefix.begin(), prefix.end());
    int delay = steps.back().delay + 15;
    for (auto it = prefix.rbegin(); it != prefix.rend(); ++it, delay += 15)
      key(it->key, false, delay);
  }
  return steps;
}
std::vector<Step> BuildUnlockSteps(const std::string &password, bool macos) {
  if (password.empty() || password.size() > 256 ||
      password.find('\0') != std::string::npos || password.find_first_of("\r\n\t") != std::string::npos)
    throw std::invalid_argument("INVALID_CREDENTIAL");
  const InputGeometry geometry{};
  auto steps = BuildInputSteps({{"type", "hotkey"}, {"keys", {macos ? "META" : "CTRL", "A"}}}, geometry);
  auto text = BuildInputSteps({{"type", "type_text"}, {"text", password}}, geometry);
  text.front().delay = 150;
  steps.insert(steps.end(), text.begin(), text.end());
  auto enter = BuildInputSteps({{"type", "key_press"}, {"key", "ENTER"}}, geometry);
  for (auto &step : enter) step.delay += 350;
  steps.insert(steps.end(), enter.begin(), enter.end());
  return steps;
}
} // namespace crossdesk::cli::automation
