#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "clipboard.h"
#include "device_controller.h"
#include "file_transfer.h"
#include "localization.h"
#include "minirtc.h"
#include "platform.h"
#include "rd_log.h"
#include "render.h"
#if _WIN32
#include "interactive_state.h"
#include "service_host.h"
#endif

#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

namespace crossdesk {

namespace {

int TranslateSdlKeypadScancodeToVk(SDL_Scancode scancode) {
  switch (scancode) {
    case SDL_SCANCODE_NUMLOCKCLEAR:
      return 0x90;
    case SDL_SCANCODE_KP_ENTER:
      return 0x0D;
    case SDL_SCANCODE_KP_0:
      return 0x60;
    case SDL_SCANCODE_KP_1:
      return 0x61;
    case SDL_SCANCODE_KP_2:
      return 0x62;
    case SDL_SCANCODE_KP_3:
      return 0x63;
    case SDL_SCANCODE_KP_4:
      return 0x64;
    case SDL_SCANCODE_KP_5:
      return 0x65;
    case SDL_SCANCODE_KP_6:
      return 0x66;
    case SDL_SCANCODE_KP_7:
      return 0x67;
    case SDL_SCANCODE_KP_8:
      return 0x68;
    case SDL_SCANCODE_KP_9:
      return 0x69;
    case SDL_SCANCODE_KP_PERIOD:
    case SDL_SCANCODE_KP_COMMA:
      return 0x6E;
    case SDL_SCANCODE_KP_DIVIDE:
      return 0x6F;
    case SDL_SCANCODE_KP_MULTIPLY:
      return 0x6A;
    case SDL_SCANCODE_KP_MINUS:
      return 0x6D;
    case SDL_SCANCODE_KP_PLUS:
      return 0x6B;
    case SDL_SCANCODE_KP_EQUALS:
      return 0xBB;
    default:
      return -1;
  }
}

int TranslateSdlKeyboardEventToVk(const SDL_KeyboardEvent& event) {
  const int keypad_key_code = TranslateSdlKeypadScancodeToVk(event.scancode);
  if (keypad_key_code >= 0) {
    return keypad_key_code;
  }

  const int key = static_cast<int>(event.key);
  if (key >= 'a' && key <= 'z') {
    return key - 'a' + 0x41;
  }
  if (key >= 'A' && key <= 'Z') {
    return key;
  }
  if (key >= '0' && key <= '9') {
    return key;
  }

  switch (key) {
    case ';':
      return 0xBA;
    case '\'':
      return 0xDE;
    case '`':
      return 0xC0;
    case ',':
      return 0xBC;
    case '.':
      return 0xBE;
    case '/':
      return 0xBF;
    case '\\':
      return 0xDC;
    case '[':
      return 0xDB;
    case ']':
      return 0xDD;
    case '-':
      return 0xBD;
    case '=':
      return 0xBB;
    default:
      break;
  }

  switch (event.scancode) {
    case SDL_SCANCODE_ESCAPE:
      return 0x1B;
    case SDL_SCANCODE_RETURN:
      return 0x0D;
    case SDL_SCANCODE_SPACE:
      return 0x20;
    case SDL_SCANCODE_BACKSPACE:
      return 0x08;
    case SDL_SCANCODE_TAB:
      return 0x09;
    case SDL_SCANCODE_PRINTSCREEN:
      return 0x2C;
    case SDL_SCANCODE_SCROLLLOCK:
      return 0x91;
    case SDL_SCANCODE_PAUSE:
      return 0x13;
    case SDL_SCANCODE_INSERT:
      return 0x2D;
    case SDL_SCANCODE_DELETE:
      return 0x2E;
    case SDL_SCANCODE_HOME:
      return 0x24;
    case SDL_SCANCODE_END:
      return 0x23;
    case SDL_SCANCODE_PAGEUP:
      return 0x21;
    case SDL_SCANCODE_PAGEDOWN:
      return 0x22;
    case SDL_SCANCODE_LEFT:
      return 0x25;
    case SDL_SCANCODE_RIGHT:
      return 0x27;
    case SDL_SCANCODE_UP:
      return 0x26;
    case SDL_SCANCODE_DOWN:
      return 0x28;
    case SDL_SCANCODE_F1:
      return 0x70;
    case SDL_SCANCODE_F2:
      return 0x71;
    case SDL_SCANCODE_F3:
      return 0x72;
    case SDL_SCANCODE_F4:
      return 0x73;
    case SDL_SCANCODE_F5:
      return 0x74;
    case SDL_SCANCODE_F6:
      return 0x75;
    case SDL_SCANCODE_F7:
      return 0x76;
    case SDL_SCANCODE_F8:
      return 0x77;
    case SDL_SCANCODE_F9:
      return 0x78;
    case SDL_SCANCODE_F10:
      return 0x79;
    case SDL_SCANCODE_F11:
      return 0x7A;
    case SDL_SCANCODE_F12:
      return 0x7B;
    case SDL_SCANCODE_CAPSLOCK:
      return 0x14;
    case SDL_SCANCODE_LSHIFT:
      return 0xA0;
    case SDL_SCANCODE_RSHIFT:
      return 0xA1;
    case SDL_SCANCODE_LCTRL:
      return 0xA2;
    case SDL_SCANCODE_RCTRL:
      return 0xA3;
    case SDL_SCANCODE_LALT:
      return 0xA4;
    case SDL_SCANCODE_RALT:
      return 0xA5;
    case SDL_SCANCODE_LGUI:
      return 0x5B;
    case SDL_SCANCODE_RGUI:
      return 0x5C;
    default:
      return -1;
  }
}

#if _WIN32
int NormalizeWindowsModifierVk(int key_code, uint32_t scan_code,
                               bool extended) {
  if (key_code != 0x10 && key_code != 0x11 && key_code != 0x12) {
    return key_code;
  }

  UINT scan_code_with_prefix = static_cast<UINT>(scan_code & 0xFF);
  if (extended) {
    scan_code_with_prefix |= 0xE000;
  }

  const UINT normalized_vk =
      MapVirtualKeyW(scan_code_with_prefix, MAPVK_VSC_TO_VK_EX);
  return normalized_vk != 0 ? static_cast<int>(normalized_vk) : key_code;
}

void PopulateWindowsKeyMetadataFromVk(int key_code, uint32_t* scan_code_out,
                                      bool* extended_out) {
  if (scan_code_out == nullptr || extended_out == nullptr) {
    return;
  }

  const UINT scan_code =
      MapVirtualKeyW(static_cast<UINT>(key_code), MAPVK_VK_TO_VSC_EX);
  if (scan_code == 0) {
    return;
  }

  *scan_code_out = static_cast<uint32_t>(scan_code & 0xFF);
  *extended_out = (scan_code & 0xFF00) != 0;
}
#endif

#if _WIN32
constexpr uint32_t kSecureDesktopInputLogIntervalMs = 2000;

bool BuildAbsoluteMousePosition(const std::vector<DisplayInfo>& displays,
                                int display_index, float normalized_x,
                                float normalized_y, int* absolute_x_out,
                                int* absolute_y_out) {
  if (absolute_x_out == nullptr || absolute_y_out == nullptr ||
      display_index < 0 || display_index >= static_cast<int>(displays.size())) {
    return false;
  }

  const DisplayInfo& display = displays[display_index];
  if (display.width <= 0 || display.height <= 0) {
    return false;
  }

  const float clamped_x = std::clamp(normalized_x, 0.0f, 1.0f);
  const float clamped_y = std::clamp(normalized_y, 0.0f, 1.0f);
  *absolute_x_out = static_cast<int>(clamped_x * display.width) + display.left;
  *absolute_y_out = static_cast<int>(clamped_y * display.height) + display.top;
  return true;
}

void LogSecureDesktopInputBlocked(uint32_t* last_tick, const char* side,
                                  const char* stage) {
  if (last_tick == nullptr) {
    return;
  }

  const uint32_t now = static_cast<uint32_t>(SDL_GetTicks());
  if (*last_tick != 0 && now - *last_tick < kSecureDesktopInputLogIntervalMs) {
    return;
  }

  *last_tick = now;
  LOG_WARN(
      "{} secure-desktop input blocked, stage={}, normal SendInput path "
      "cannot drive the Windows password UI",
      side != nullptr ? side : "unknown", stage != nullptr ? stage : "");
}
#endif

}  // namespace

void Render::OnSignalMessageCb(const char* message, size_t size,
                               void* user_data) {
  Render* render = (Render*)user_data;
  if (!render || !message || size == 0) {
    return;
  }
  std::string s(message, size);
  auto j = nlohmann::json::parse(s, nullptr, false);
  if (j.is_discarded() || !j.contains("type") || !j["type"].is_string()) {
    return;
  }
  std::string type = j["type"].get<std::string>();
  if (type == "presence") {
    if (j.contains("devices") && j["devices"].is_array()) {
      for (auto& dev : j["devices"]) {
        if (!dev.is_object()) {
          continue;
        }
        if (!dev.contains("id") || !dev["id"].is_string()) {
          continue;
        }
        if (!dev.contains("online") || !dev["online"].is_boolean()) {
          continue;
        }
        std::string id = dev["id"].get<std::string>();
        bool online = dev["online"].get<bool>();
        render->device_presence_.SetOnline(id, online);
        {
          std::lock_guard<std::mutex> lock(
              render->pending_presence_probe_mutex_);
          if (render->pending_presence_probe_ &&
              render->pending_presence_remote_id_ == id) {
            render->pending_presence_result_ready_ = true;
            render->pending_presence_online_ = online;
          }
        }
      }
    }
  } else if (type == "presence_update") {
    if (j.contains("id") && j["id"].is_string() && j.contains("online") &&
        j["online"].is_boolean()) {
      std::string id = j["id"].get<std::string>();
      bool online = j["online"].get<bool>();
      if (!id.empty()) {
        render->device_presence_.SetOnline(id, online);
        {
          std::lock_guard<std::mutex> lock(
              render->pending_presence_probe_mutex_);
          if (render->pending_presence_probe_ &&
              render->pending_presence_remote_id_ == id) {
            render->pending_presence_result_ready_ = true;
            render->pending_presence_online_ = online;
          }
        }
      }
    }
  }
}

bool Render::IsModifierVkKey(int key_code) {
  switch (key_code) {
    case 0x10:  // VK_SHIFT
    case 0x11:  // VK_CONTROL
    case 0x12:  // VK_MENU(ALT)
    case 0x5B:  // VK_LWIN
    case 0x5C:  // VK_RWIN
    case 0xA0:  // VK_LSHIFT
    case 0xA1:  // VK_RSHIFT
    case 0xA2:  // VK_LCONTROL
    case 0xA3:  // VK_RCONTROL
    case 0xA4:  // VK_LMENU
    case 0xA5:  // VK_RMENU
      return true;
    default:
      return false;
  }
}

void Render::TrackPressedKeyState(int key_code, bool is_down) {
  if (!IsWaylandSession() && !IsModifierVkKey(key_code)) {
    return;
  }

  std::lock_guard<std::mutex> lock(pressed_keyboard_keys_mutex_);
  if (is_down) {
    pressed_keyboard_keys_.insert(key_code);
  } else {
    pressed_keyboard_keys_.erase(key_code);
  }
}

void Render::ForceReleasePressedKeys() {
  std::vector<int> pressed_keys;
  {
    std::lock_guard<std::mutex> lock(pressed_keyboard_keys_mutex_);
    if (pressed_keyboard_keys_.empty()) {
      return;
    }
    pressed_keys.assign(pressed_keyboard_keys_.begin(),
                        pressed_keyboard_keys_.end());
    pressed_keyboard_keys_.clear();
  }

  for (int key_code : pressed_keys) {
    SendKeyCommand(key_code, false);
  }
}

int Render::SendKeyCommand(int key_code, bool is_down, uint32_t scan_code,
                           bool extended) {
  RemoteAction remote_action{};
  remote_action.type = ControlType::keyboard;
  if (is_down) {
    remote_action.k.flag = KeyFlag::key_down;
  } else {
    remote_action.k.flag = KeyFlag::key_up;
  }

#if _WIN32
  if (scan_code == 0) {
    PopulateWindowsKeyMetadataFromVk(key_code, &scan_code, &extended);
  }
  key_code = NormalizeWindowsModifierVk(key_code, scan_code, extended);
#endif

  remote_action.k.key_value = key_code;
  remote_action.k.scan_code = scan_code;
  remote_action.k.extended = extended;

  std::string target_id = controlled_remote_id_.empty() ? focused_remote_id_
                                                        : controlled_remote_id_;
  if (!target_id.empty()) {
    if (client_properties_.find(target_id) != client_properties_.end()) {
      auto props = client_properties_[target_id];
      if (props->connection_status_ == ConnectionStatus::Connected &&
          props->peer_) {
        std::string msg = remote_action.to_json();
        int ret = SendReliableDataFrame(props->peer_, msg.c_str(), msg.size(),
                                        props->keyboard_label_.c_str());
        if (ret != 0) {
          LOG_WARN("Send keyboard command failed, remote_id={}, ret={}",
                   target_id, ret);
        }
      }
    }
  }

  TrackPressedKeyState(key_code, is_down);

  return 0;
}

int Render::ProcessKeyboardEvent(const SDL_Event& event) {
  if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
    return -1;
  }

  if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat) {
    return 0;
  }

  const int key_code = TranslateSdlKeyboardEventToVk(event.key);
  if (key_code < 0) {
    return 0;
  }

  return SendKeyCommand(key_code, event.type == SDL_EVENT_KEY_DOWN);
}

int Render::ProcessMouseEvent(const SDL_Event& event) {
  controlled_remote_id_ = "";
  RemoteAction remote_action;
  float cursor_x = last_mouse_event.motion.x;
  float cursor_y = last_mouse_event.motion.y;

  auto normalize_cursor_to_window_space = [&](float* x, float* y) {
    if (!x || !y || !stream_window_) {
      return;
    }

    int window_width = 0;
    int window_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    SDL_GetWindowSize(stream_window_, &window_width, &window_height);
    SDL_GetWindowSizeInPixels(stream_window_, &pixel_width, &pixel_height);

    if (window_width <= 0 || window_height <= 0 || pixel_width <= 0 ||
        pixel_height <= 0) {
      return;
    }

    if ((window_width != pixel_width || window_height != pixel_height) &&
        (*x > static_cast<float>(window_width) + 1.0f ||
         *y > static_cast<float>(window_height) + 1.0f)) {
      const float scale_x =
          static_cast<float>(window_width) / static_cast<float>(pixel_width);
      const float scale_y =
          static_cast<float>(window_height) / static_cast<float>(pixel_height);
      *x *= scale_x;
      *y *= scale_y;

      static bool logged_pixel_to_window_conversion = false;
      if (!logged_pixel_to_window_conversion) {
        LOG_INFO(
            "Mouse coordinate space converted from pixels to window units: "
            "window={}x{}, pixels={}x{}, scale=({:.4f},{:.4f})",
            window_width, window_height, pixel_width, pixel_height, scale_x,
            scale_y);
        logged_pixel_to_window_conversion = true;
      }
    }
  };

  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    cursor_x = event.motion.x;
    cursor_y = event.motion.y;
    normalize_cursor_to_window_space(&cursor_x, &cursor_y);
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    cursor_x = event.button.x;
    cursor_y = event.button.y;
    normalize_cursor_to_window_space(&cursor_x, &cursor_y);
  } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    cursor_x = last_mouse_event.motion.x;
    cursor_y = last_mouse_event.motion.y;
  }

  const bool is_pointer_position_event =
      (event.type == SDL_EVENT_MOUSE_MOTION ||
       event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       event.type == SDL_EVENT_MOUSE_BUTTON_UP);

  // std::shared_lock lock(client_properties_mutex_);
  for (auto& it : client_properties_) {
    auto props = it.second;
    if (!props->control_mouse_) {
      continue;
    }

    const bool file_transfer_window_hovered =
        props->file_transfer_.file_transfer_window_hovered_;
    const bool overlay_hovered = props->control_bar_hovered_ ||
                                 props->display_selectable_hovered_ ||
                                 file_transfer_window_hovered;

    const SDL_FRect render_rect = props->stream_render_rect_f_;
    if (render_rect.w <= 1.0f || render_rect.h <= 1.0f) {
      continue;
    }

    if (is_pointer_position_event && cursor_x >= render_rect.x &&
        cursor_x <= render_rect.x + render_rect.w &&
        cursor_y >= render_rect.y &&
        cursor_y <= render_rect.y + render_rect.h) {
      controlled_remote_id_ = it.first;
      last_mouse_event.motion.x = cursor_x;
      last_mouse_event.motion.y = cursor_y;
      last_mouse_event.button.x = cursor_x;
      last_mouse_event.button.y = cursor_y;

      remote_action.m.x = (cursor_x - render_rect.x) / render_rect.w;
      remote_action.m.y = (cursor_y - render_rect.y) / render_rect.h;
      remote_action.m.x = std::clamp(remote_action.m.x, 0.0f, 1.0f);
      remote_action.m.y = std::clamp(remote_action.m.y, 0.0f, 1.0f);

      if (SDL_EVENT_MOUSE_BUTTON_DOWN == event.type) {
        remote_action.type = ControlType::mouse;
        if (SDL_BUTTON_LEFT == event.button.button) {
          remote_action.m.flag = MouseFlag::left_down;
        } else if (SDL_BUTTON_RIGHT == event.button.button) {
          remote_action.m.flag = MouseFlag::right_down;
        } else if (SDL_BUTTON_MIDDLE == event.button.button) {
          remote_action.m.flag = MouseFlag::middle_down;
        }
      } else if (SDL_EVENT_MOUSE_BUTTON_UP == event.type) {
        remote_action.type = ControlType::mouse;
        if (SDL_BUTTON_LEFT == event.button.button) {
          remote_action.m.flag = MouseFlag::left_up;
        } else if (SDL_BUTTON_RIGHT == event.button.button) {
          remote_action.m.flag = MouseFlag::right_up;
        } else if (SDL_BUTTON_MIDDLE == event.button.button) {
          remote_action.m.flag = MouseFlag::middle_up;
        }
      } else if (SDL_EVENT_MOUSE_MOTION == event.type) {
        remote_action.type = ControlType::mouse;
        remote_action.m.flag = MouseFlag::move;
      }

      if (overlay_hovered) {
        break;
      }
      if (props->peer_) {
        std::string msg = remote_action.to_json();
        SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                      props->mouse_label_.c_str());
      }
    } else if (SDL_EVENT_MOUSE_WHEEL == event.type &&
               last_mouse_event.button.x >= render_rect.x &&
               last_mouse_event.button.x <= render_rect.x + render_rect.w &&
               last_mouse_event.button.y >= render_rect.y &&
               last_mouse_event.button.y <= render_rect.y + render_rect.h) {
      float scroll_x = event.wheel.x;
      float scroll_y = event.wheel.y;
      if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        scroll_x = -scroll_x;
        scroll_y = -scroll_y;
      }

      remote_action.type = ControlType::mouse;

      auto roundUp = [](float value) -> int {
        if (value > 0) {
          return static_cast<int>(std::ceil(value));
        } else if (value < 0) {
          return static_cast<int>(std::floor(value));
        }
        return 0;
      };

      if (std::abs(scroll_y) >= std::abs(scroll_x)) {
        remote_action.m.flag = MouseFlag::wheel_vertical;
        remote_action.m.s = roundUp(scroll_y);
      } else {
        remote_action.m.flag = MouseFlag::wheel_horizontal;
        remote_action.m.s = roundUp(scroll_x);
      }

      remote_action.m.x = (last_mouse_event.button.x - render_rect.x) /
                          (std::max)(render_rect.w, 1.0f);
      remote_action.m.y = (last_mouse_event.button.y - render_rect.y) /
                          (std::max)(render_rect.h, 1.0f);
      remote_action.m.x = std::clamp(remote_action.m.x, 0.0f, 1.0f);
      remote_action.m.y = std::clamp(remote_action.m.y, 0.0f, 1.0f);

      if (overlay_hovered) {
        continue;
      }
      if (props->peer_) {
        std::string msg = remote_action.to_json();
        SendDataFrame(props->peer_, msg.c_str(), msg.size(),
                      props->mouse_label_.c_str());
      }
    }
  }

  return 0;
}

void Render::SdlCaptureAudioIn(void* userdata, Uint8* stream, int len) {
  Render* render = (Render*)userdata;
  if (!render) {
    return;
  }

  if (1) {
    // std::shared_lock lock(render->client_properties_mutex_);
    for (const auto& it : render->client_properties_) {
      auto props = it.second;
      if (props->connection_status_ == ConnectionStatus::Connected) {
        if (props->peer_) {
          SendAudioFrame(props->peer_, (const char*)stream, len,
                         render->audio_label_.c_str());
        }
      }
    }

  } else {
    memcpy(render->audio_buffer_, stream, len);
    render->audio_len_ = len;
    SDL_Delay(10);
    render->audio_buffer_fresh_ = true;
  }
}

void Render::SdlCaptureAudioOut([[maybe_unused]] void* userdata,
                                [[maybe_unused]] Uint8* stream,
                                [[maybe_unused]] int len) {
  // Render *render = (Render *)userdata;
  // for (auto it : render->client_properties_) {
  //   auto props = it.second;
  //   if (props->connection_status_ == SignalStatus::SignalConnected) {
  //     SendAudioFrame(props->peer_, (const char *)stream, len);
  //   }
  // }

  // if (!render->audio_buffer_fresh_) {
  //   return;
  // }

  // SDL_memset(stream, 0, len);

  // if (render->audio_len_ == 0) {
  //   return;
  // } else {
  // }

  // len = (len > render->audio_len_ ? render->audio_len_ : len);
  // SDL_MixAudioFormat(stream, render->audio_buffer_, AUDIO_S16LSB, len,
  //                    SDL_MIX_MAXVOLUME);
  // render->audio_buffer_fresh_ = false;
}

void Render::OnReceiveVideoBufferCb(const XVideoFrame* video_frame,
                                    const char* user_id, size_t user_id_size,
                                    const char* src_id, size_t src_id_size,
                                    void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(render->client_properties_mutex_);
  if (render->client_properties_.find(remote_id) ==
      render->client_properties_.end()) {
    return;
  }
  SubStreamWindowProperties* props =
      render->client_properties_.find(remote_id)->second.get();

  if (props->connection_established_) {
    {
      std::lock_guard<std::mutex> lock(props->video_frame_mutex_);

      if (!props->back_frame_) {
        props->back_frame_ =
            std::make_shared<std::vector<unsigned char>>(video_frame->size);
      }
      if (props->back_frame_->size() != video_frame->size) {
        props->back_frame_->resize(video_frame->size);
      }

      std::memcpy(props->back_frame_->data(), video_frame->data,
                  video_frame->size);

      const bool size_changed = (props->video_width_ != video_frame->width) ||
                                (props->video_height_ != video_frame->height);
      if (size_changed) {
        props->render_rect_dirty_ = true;
      }

      props->video_width_ = video_frame->width;
      props->video_height_ = video_frame->height;
      props->video_size_ = video_frame->size;

      props->front_frame_.swap(props->back_frame_);
    }

    SDL_Event event;
    event.type = render->STREAM_REFRESH_EVENT;
    event.user.data1 = props;
    SDL_PushEvent(&event);
    props->streaming_ = true;

    if (props->net_traffic_stats_button_pressed_) {
      props->frame_count_++;
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - props->last_time_)
                         .count();

      if (elapsed >= 1000) {
        props->fps_ = props->frame_count_ * 1000 / elapsed;
        props->frame_count_ = 0;
        props->last_time_ = now;
      }
    }
  }
}

void Render::OnReceiveAudioBufferCb(const char* data, size_t size,
                                    const char* user_id, size_t user_id_size,
                                    const char* src_id, size_t src_id_size,
                                    void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  render->audio_buffer_fresh_ = true;

  if (render->output_stream_) {
    int pushed = SDL_PutAudioStreamData(
        render->output_stream_, (const Uint8*)data, static_cast<int>(size));
    if (pushed < 0) {
      LOG_ERROR("Failed to push audio data: {}", SDL_GetError());
    }
  }
}

void Render::OnReceiveDataBufferCb(const char* data, size_t size,
                                   const char* user_id, size_t user_id_size,
                                   const char* src_id, size_t src_id_size,
                                   void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  std::string source_id = std::string(src_id, src_id_size);
  if (source_id == render->file_label_) {
    std::string remote_user_id = std::string(user_id, user_id_size);

    static FileReceiver receiver;
    // Update output directory from config
    std::string configured_path =
        render->config_center_->GetFileTransferSavePath();
    if (!configured_path.empty()) {
      receiver.SetOutputDir(std::filesystem::u8path(configured_path));
    } else if (receiver.OutputDir().empty()) {
      receiver = FileReceiver();  // re-init with default desktop path
    }
    receiver.SetOnSendAck([render,
                           remote_user_id](const FileTransferAck& ack) -> int {
      bool is_server_sending = remote_user_id.rfind("C-", 0) != 0;
      if (is_server_sending) {
        auto props =
            render->GetSubStreamWindowPropertiesByRemoteId(remote_user_id);
        if (props) {
          PeerPtr* peer = props->peer_;
          return SendReliableDataFrame(
              peer, reinterpret_cast<const char*>(&ack),
              sizeof(FileTransferAck), render->file_feedback_label_.c_str());
        }
      }

      return SendReliableDataFrame(
          render->peer_, reinterpret_cast<const char*>(&ack),
          sizeof(FileTransferAck), render->file_feedback_label_.c_str());
    });

    receiver.OnData(data, size);
    return;
  } else if (source_id == render->clipboard_label_) {
    if (size > 0) {
      std::string remote_user_id(user_id, user_id_size);
      auto props =
          render->GetSubStreamWindowPropertiesByRemoteId(remote_user_id);
      if (props && !props->enable_mouse_control_) {
        return;
      }

      std::string clipboard_text(data, size);
      if (!Clipboard::SetText(clipboard_text)) {
        LOG_ERROR("Failed to set clipboard content from remote");
      }
    }
    return;
  } else if (source_id == render->file_feedback_label_) {
    if (size < sizeof(FileTransferAck)) {
      LOG_ERROR("FileTransferAck: buffer too small, size={}", size);
      return;
    }

    FileTransferAck ack{};
    memcpy(&ack, data, sizeof(FileTransferAck));

    if (ack.magic != kFileAckMagic) {
      LOG_ERROR(
          "FileTransferAck: invalid magic, got 0x{:08X}, expected 0x{:08X}",
          ack.magic, kFileAckMagic);
      return;
    }

    std::shared_ptr<SubStreamWindowProperties> props = nullptr;
    {
      std::shared_lock lock(render->file_id_to_props_mutex_);
      auto it = render->file_id_to_props_.find(ack.file_id);
      if (it != render->file_id_to_props_.end()) {
        props = it->second.lock();
      }
    }

    Render::FileTransferState* state = nullptr;
    if (!props) {
      {
        std::shared_lock lock(render->file_id_to_transfer_state_mutex_);
        auto it = render->file_id_to_transfer_state_.find(ack.file_id);
        if (it != render->file_id_to_transfer_state_.end()) {
          state = it->second;
        }
      }

      if (!state) {
        LOG_WARN("FileTransferAck: no props/state found for file_id={}",
                 ack.file_id);
        return;
      }
    } else {
      state = &props->file_transfer_;
    }

    // Update progress based on ACK
    state->file_sent_bytes_ = ack.acked_offset;
    state->file_total_bytes_ = ack.total_size;

    uint32_t rate_bps = 0;
    {
      if (props) {
        uint32_t data_channel_bitrate =
            props->net_traffic_stats_.data_outbound_stats.bitrate;

        if (data_channel_bitrate > 0 && state->file_sending_.load()) {
          rate_bps = static_cast<uint32_t>(data_channel_bitrate * 0.99f);

          uint32_t current_rate = state->file_send_rate_bps_.load();
          if (current_rate > 0) {
            // 70% old + 30% new for smoother display
            rate_bps =
                static_cast<uint32_t>(current_rate * 0.7 + rate_bps * 0.3);
          }
        } else {
          rate_bps = state->file_send_rate_bps_.load();
        }
      } else {
        // Global transfer: no per-connection bitrate available.
        // Estimate send rate from ACKed bytes delta over time.
        const uint32_t current_rate = state->file_send_rate_bps_.load();
        uint32_t estimated_rate_bps = 0;
        const auto now = std::chrono::steady_clock::now();

        uint64_t last_bytes = 0;
        std::chrono::steady_clock::time_point last_time;
        {
          std::lock_guard<std::mutex> lock(state->file_transfer_mutex_);
          last_bytes = state->file_send_last_bytes_;
          last_time = state->file_send_last_update_time_;
        }

        if (state->file_sending_.load() && ack.acked_offset >= last_bytes) {
          const uint64_t delta_bytes = ack.acked_offset - last_bytes;
          const double delta_seconds =
              std::chrono::duration<double>(now - last_time).count();

          if (delta_seconds > 0.0 && delta_bytes > 0) {
            const double bps =
                (static_cast<double>(delta_bytes) * 8.0) / delta_seconds;
            if (bps > 0.0) {
              const double capped =
                  (std::min)(bps, static_cast<double>(
                                      (std::numeric_limits<uint32_t>::max)()));
              estimated_rate_bps = static_cast<uint32_t>(capped);
            }
          }
        }

        if (estimated_rate_bps > 0 && current_rate > 0) {
          // 70% old + 30% new for smoother display
          rate_bps = static_cast<uint32_t>(current_rate * 0.7 +
                                           estimated_rate_bps * 0.3);
        } else if (estimated_rate_bps > 0) {
          rate_bps = estimated_rate_bps;
        } else {
          rate_bps = current_rate;
        }
      }

      state->file_send_rate_bps_ = rate_bps;
      state->file_send_last_bytes_ = ack.acked_offset;
      auto now = std::chrono::steady_clock::now();
      state->file_send_last_update_time_ = now;
    }

    // Update file transfer list: update progress and rate
    {
      std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
      for (auto& info : state->file_transfer_list_) {
        if (info.file_id == ack.file_id) {
          info.sent_bytes = ack.acked_offset;
          info.file_size = ack.total_size;
          info.rate_bps = rate_bps;
          break;
        }
      }
    }

    // Check if transfer is completed
    if ((ack.flags & 0x01) != 0) {
      // Transfer completed - receiver has finished receiving the file
      // Reopen window if it was closed by user
      state->file_transfer_window_visible_ = true;
      state->file_sending_ = false;  // Mark sending as finished
      LOG_INFO(
          "File transfer completed via ACK, file_id={}, total_size={}, "
          "acked_offset={}",
          ack.file_id, ack.total_size, ack.acked_offset);

      // Update file transfer list: mark as completed
      {
        std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
        for (auto& info : state->file_transfer_list_) {
          if (info.file_id == ack.file_id) {
            info.status =
                Render::FileTransferState::FileTransferStatus::Completed;
            info.sent_bytes = ack.total_size;
            break;
          }
        }
      }

      // Unregister file_id mapping after completion
      {
        if (props) {
          std::lock_guard<std::shared_mutex> lock(
              render->file_id_to_props_mutex_);
          render->file_id_to_props_.erase(ack.file_id);
        } else {
          std::lock_guard<std::shared_mutex> lock(
              render->file_id_to_transfer_state_mutex_);
          render->file_id_to_transfer_state_.erase(ack.file_id);
        }
      }

      // Process next file in queue
      render->ProcessFileQueue(props);
    }

    return;
  }

  std::string json_str(data, size);
  RemoteAction remote_action{};
  if (!remote_action.from_json(json_str)) {
    LOG_ERROR("Failed to parse RemoteAction JSON payload");
    return;
  }

  std::string remote_id(user_id, user_id_size);
  if (remote_action.type == ControlType::service_status) {
    auto props_it = render->client_properties_.find(remote_id);
    if (props_it != render->client_properties_.end()) {
      render->ApplyRemoteServiceStatus(*props_it->second, remote_action.ss);
    }
    return;
  }

  if (remote_action.type == ControlType::service_command) {
#if _WIN32
    if (remote_action.c.flag == ServiceCommandFlag::send_sas) {
      render->pending_windows_service_sas_.store(true,
                                                 std::memory_order_relaxed);
    }
#endif
    return;
  }

  // std::shared_lock lock(render->client_properties_mutex_);
  if (remote_action.type == ControlType::host_infomation) {
    if (render->client_properties_.find(remote_id) !=
        render->client_properties_.end()) {
      // client mode
      auto props = render->client_properties_.find(remote_id)->second;
      if (props && props->remote_host_name_.empty()) {
        props->remote_host_name_ = std::string(remote_action.i.host_name,
                                               remote_action.i.host_name_size);
        LOG_INFO("Remote hostname: [{}]", props->remote_host_name_);

        for (int i = 0; i < remote_action.i.display_num; i++) {
          props->display_info_list_.push_back(
              DisplayInfo(remote_action.i.display_list[i],
                          remote_action.i.left[i], remote_action.i.top[i],
                          remote_action.i.right[i], remote_action.i.bottom[i]));
        }
      }
      FreeRemoteAction(remote_action);
    } else {
      // server mode
      render->connection_host_names_[remote_id] = std::string(
          remote_action.i.host_name, remote_action.i.host_name_size);
      LOG_INFO("Remote hostname: [{}]",
               render->connection_host_names_[remote_id]);
      FreeRemoteAction(remote_action);
    }
  } else {
    // remote
#if _WIN32
    if (render->local_service_status_received_ &&
        render->local_service_available_ &&
        IsSecureDesktopInteractionRequired(render->local_interactive_stage_)) {
      if (remote_action.type == ControlType::mouse) {
        int absolute_x = 0;
        int absolute_y = 0;
        if (!BuildAbsoluteMousePosition(render->display_info_list_,
                                        render->selected_display_,
                                        remote_action.m.x, remote_action.m.y,
                                        &absolute_x, &absolute_y)) {
          LOG_WARN(
              "Secure desktop mouse injection skipped, invalid display "
              "mapping: display_index={}, x={}, y={}",
              render->selected_display_, remote_action.m.x, remote_action.m.y);
          return;
        }

        const std::string response = SendCrossDeskSecureDesktopMouseInput(
            absolute_x, absolute_y, remote_action.m.s,
            static_cast<int>(remote_action.m.flag), 1000);
        auto json = nlohmann::json::parse(response, nullptr, false);
        if (json.is_discarded() || !json.value("ok", false)) {
          LogSecureDesktopInputBlocked(
              &render->last_local_secure_input_block_log_tick_, "local",
              render->local_interactive_stage_.c_str());
          LOG_WARN(
              "Secure desktop mouse injection failed, x={}, y={}, wheel={}, "
              "flag={}, response={}",
              absolute_x, absolute_y, remote_action.m.s,
              static_cast<int>(remote_action.m.flag), response);
        }
        return;
      }

      if (remote_action.type == ControlType::keyboard) {
        const int key_code = static_cast<int>(remote_action.k.key_value);
        const bool is_down = remote_action.k.flag == KeyFlag::key_down;
        const std::string response = SendCrossDeskSecureDesktopKeyInput(
            key_code, is_down, remote_action.k.scan_code,
            remote_action.k.extended, 1000);
        auto json = nlohmann::json::parse(response, nullptr, false);
        if (json.is_discarded() || !json.value("ok", false)) {
          LogSecureDesktopInputBlocked(
              &render->last_local_secure_input_block_log_tick_, "local",
              render->local_interactive_stage_.c_str());
          LOG_WARN(
              "Secure desktop keyboard injection failed, key_code={}, "
              "is_down={}, response={}",
              key_code, is_down, response);
        }
        return;
      }
    }
#endif
    if (remote_action.type == ControlType::mouse && render->mouse_controller_) {
      render->mouse_controller_->SendMouseCommand(remote_action,
                                                  render->selected_display_);
    } else if (remote_action.type == ControlType::audio_capture) {
      if (remote_action.a && !render->start_speaker_capturer_)
        render->StartSpeakerCapturer();
      else if (!remote_action.a && render->start_speaker_capturer_)
        render->StopSpeakerCapturer();
    } else if (remote_action.type == ControlType::keyboard &&
               render->keyboard_capturer_) {
      render->keyboard_capturer_->SendKeyboardCommand(
          (int)remote_action.k.key_value,
          remote_action.k.flag == KeyFlag::key_down, remote_action.k.scan_code,
          remote_action.k.extended);
    } else if (remote_action.type == ControlType::display_id &&
               render->screen_capturer_) {
      render->selected_display_ = remote_action.d;
      render->screen_capturer_->SwitchTo(remote_action.d);
    }
  }
}

void Render::OnSignalStatusCb(SignalStatus status, const char* user_id,
                              size_t user_id_size, void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  std::string client_id(user_id, user_id_size);
  if (client_id == render->client_id_) {
    render->signal_status_ = status;
    if (SignalStatus::SignalConnecting == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalConnected == status) {
      render->signal_connected_ = true;
      render->need_to_send_recent_connections_ = true;
      LOG_INFO("[{}] connected to signal server", client_id);
    } else if (SignalStatus::SignalFailed == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalClosed == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalReconnecting == status) {
      render->signal_connected_ = false;
    } else if (SignalStatus::SignalServerClosed == status) {
      render->signal_connected_ = false;
    }
  } else {
    if (client_id.rfind("C-", 0) != 0) {
      return;
    }

    std::string remote_id(client_id.begin() + 2, client_id.end());
    // std::shared_lock lock(render->client_properties_mutex_);
    if (render->client_properties_.find(remote_id) ==
        render->client_properties_.end()) {
      return;
    }
    auto props = render->client_properties_.find(remote_id)->second;
    props->signal_status_ = status;
    if (SignalStatus::SignalConnecting == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalConnected == status) {
      props->signal_connected_ = true;
      LOG_INFO("[{}] connected to signal server", remote_id);
    } else if (SignalStatus::SignalFailed == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalClosed == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalReconnecting == status) {
      props->signal_connected_ = false;
    } else if (SignalStatus::SignalServerClosed == status) {
      props->signal_connected_ = false;
    }
  }
}

void Render::OnConnectionStatusCb(ConnectionStatus status, const char* user_id,
                                  const size_t user_id_size, void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) return;

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(render->client_properties_mutex_);
  auto it = render->client_properties_.find(remote_id);
  auto props = (it != render->client_properties_.end()) ? it->second : nullptr;

  if (props) {
    render->is_client_mode_ = true;
    render->show_connection_status_window_ = true;
    props->connection_status_ = status;

    switch (status) {
      case ConnectionStatus::Connected: {
        render->ResetRemoteServiceStatus(*props);
        {
          RemoteAction remote_action;
          remote_action.i.display_num = render->display_info_list_.size();
          remote_action.i.display_list =
              (char**)malloc(remote_action.i.display_num * sizeof(char*));
          remote_action.i.left =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.top =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.right =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.bottom =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          for (int i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     render->display_info_list_[i].name);
            remote_action.i.display_list[i] =
                (char*)malloc(render->display_info_list_[i].name.length() + 1);
            strncpy(remote_action.i.display_list[i],
                    render->display_info_list_[i].name.c_str(),
                    render->display_info_list_[i].name.length());
            remote_action.i
                .display_list[i][render->display_info_list_[i].name.length()] =
                '\0';
            remote_action.i.left[i] = render->display_info_list_[i].left;
            remote_action.i.top[i] = render->display_info_list_[i].top;
            remote_action.i.right[i] = render->display_info_list_[i].right;
            remote_action.i.bottom[i] = render->display_info_list_[i].bottom;
          }

          std::string host_name = GetHostName();
          remote_action.type = ControlType::host_infomation;
          memcpy(&remote_action.i.host_name, host_name.data(),
                 host_name.size());
          remote_action.i.host_name[host_name.size()] = '\0';
          remote_action.i.host_name_size = host_name.size();

          std::string msg = remote_action.to_json();
          int ret = SendReliableDataFrame(props->peer_, msg.data(), msg.size(),
                                          render->control_data_label_.c_str());
          FreeRemoteAction(remote_action);
        }

        if (!render->need_to_create_stream_window_ &&
            !render->client_properties_.empty()) {
          render->need_to_create_stream_window_ = true;
        }
        props->connection_established_ = true;
        props->stream_render_rect_ = {
            0, (int)render->title_bar_height_,
            (int)render->stream_window_width_,
            (int)(render->stream_window_height_ - render->title_bar_height_)};
        props->stream_render_rect_f_ = {
            0.0f, render->title_bar_height_, render->stream_window_width_,
            render->stream_window_height_ - render->title_bar_height_};
        render->start_keyboard_capturer_ = true;
        break;
      }
      case ConnectionStatus::Disconnected:
      case ConnectionStatus::Failed:
      case ConnectionStatus::Closed: {
        props->connection_established_ = false;
        props->enable_mouse_control_ = false;
        render->ResetRemoteServiceStatus(*props);

        {
          std::lock_guard<std::mutex> lock(props->video_frame_mutex_);
          props->front_frame_.reset();
          props->back_frame_.reset();
          props->video_width_ = 0;
          props->video_height_ = 0;
          props->video_size_ = 0;
          props->render_rect_dirty_ = true;
          props->stream_cleanup_pending_ = true;
        }

        SDL_Event event;
        event.type = render->STREAM_REFRESH_EVENT;
        event.user.data1 = props.get();
        SDL_PushEvent(&event);

        render->focus_on_stream_window_ = false;

        break;
      }
      case ConnectionStatus::IncorrectPassword: {
        render->password_validating_ = false;
        render->password_validating_time_++;
        if (render->connect_button_pressed_) {
          render->connect_button_pressed_ = false;
          props->connection_established_ = false;
          render->connect_button_label_ =
              localization::connect[render->localization_language_index_];
        }
        break;
      }
      case ConnectionStatus::NoSuchTransmissionId: {
        if (render->connect_button_pressed_) {
          props->connection_established_ = false;
          render->connect_button_label_ =
              localization::connect[render->localization_language_index_];
        }
        break;
      }
      default:
        break;
    }
  } else {
    render->is_client_mode_ = false;
    render->show_connection_status_window_ = true;
    render->connection_status_[remote_id] = status;

    switch (status) {
      case ConnectionStatus::Connected: {
#if _WIN32
        render->last_windows_service_status_tick_ = 0;
#endif
        {
          RemoteAction remote_action;
          remote_action.i.display_num = render->display_info_list_.size();
          remote_action.i.display_list =
              (char**)malloc(remote_action.i.display_num * sizeof(char*));
          remote_action.i.left =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.top =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.right =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          remote_action.i.bottom =
              (int*)malloc(remote_action.i.display_num * sizeof(int));
          for (int i = 0; i < remote_action.i.display_num; i++) {
            LOG_INFO("Local display [{}:{}]", i + 1,
                     render->display_info_list_[i].name);
            remote_action.i.display_list[i] =
                (char*)malloc(render->display_info_list_[i].name.length() + 1);
            strncpy(remote_action.i.display_list[i],
                    render->display_info_list_[i].name.c_str(),
                    render->display_info_list_[i].name.length());
            remote_action.i
                .display_list[i][render->display_info_list_[i].name.length()] =
                '\0';
            remote_action.i.left[i] = render->display_info_list_[i].left;
            remote_action.i.top[i] = render->display_info_list_[i].top;
            remote_action.i.right[i] = render->display_info_list_[i].right;
            remote_action.i.bottom[i] = render->display_info_list_[i].bottom;
          }

          std::string host_name = GetHostName();
          remote_action.type = ControlType::host_infomation;
          memcpy(&remote_action.i.host_name, host_name.data(),
                 host_name.size());
          remote_action.i.host_name[host_name.size()] = '\0';
          remote_action.i.host_name_size = host_name.size();

          std::string msg = remote_action.to_json();
          int ret = SendReliableDataFrame(render->peer_, msg.data(), msg.size(),
                                          render->control_data_label_.c_str());
          FreeRemoteAction(remote_action);
        }

        render->need_to_create_server_window_ = true;
        render->is_server_mode_ = true;
        render->start_screen_capturer_ = true;
        render->start_speaker_capturer_ = true;
        render->remote_client_id_ = remote_id;
        render->start_mouse_controller_ = true;
        if (std::all_of(render->connection_status_.begin(),
                        render->connection_status_.end(), [](const auto& kv) {
                          return kv.first.find("web") != std::string::npos;
                        })) {
          render->show_cursor_ = true;
        }

        break;
      }
      case ConnectionStatus::Disconnected:
      case ConnectionStatus::Failed:
      case ConnectionStatus::Closed: {
        if (std::all_of(render->connection_status_.begin(),
                        render->connection_status_.end(), [](const auto& kv) {
                          return kv.second == ConnectionStatus::Closed ||
                                 kv.second == ConnectionStatus::Failed ||
                                 kv.second == ConnectionStatus::Disconnected;
                        })) {
          render->need_to_destroy_server_window_ = true;
          render->is_server_mode_ = false;
#if defined(__linux__) && !defined(__APPLE__)
          if (IsWaylandSession()) {
            // Keep Wayland capture session warm to avoid black screen on
            // subsequent reconnects.
            render->start_screen_capturer_ = true;
            LOG_INFO(
                "Keeping Wayland screen capturer running after "
                "disconnect to preserve reconnect stability");
          } else {
            render->start_screen_capturer_ = false;
          }
#else
          render->start_screen_capturer_ = false;
#endif
          render->start_speaker_capturer_ = false;
          render->start_mouse_controller_ = false;
          render->start_keyboard_capturer_ = false;
          render->remote_client_id_ = "";
          if (props) props->connection_established_ = false;
          if (render->audio_capture_) {
            render->StopSpeakerCapturer();
            render->audio_capture_ = false;
          }

          render->connection_status_.erase(remote_id);
          render->connection_host_names_.erase(remote_id);
          if (render->screen_capturer_) {
            render->screen_capturer_->ResetToInitialMonitor();
          }
        }

        if (std::all_of(render->connection_status_.begin(),
                        render->connection_status_.end(), [](const auto& kv) {
                          return kv.first.find("web") == std::string::npos;
                        })) {
          render->show_cursor_ = false;
        }

        break;
      }
      default:
        break;
    }
  }
}

void Render::OnNetStatusReport(const char* client_id, size_t client_id_size,
                               TraversalMode mode,
                               const XNetTrafficStats* net_traffic_stats,
                               const char* user_id, const size_t user_id_size,
                               void* user_data) {
  Render* render = (Render*)user_data;
  if (!render) {
    return;
  }

  if (strchr(client_id, '@') != nullptr && strchr(user_id, '-') == nullptr) {
    std::string id, password;
    const char* at_pos = strchr(client_id, '@');
    if (at_pos == nullptr) {
      id = client_id;
      password.clear();
    } else {
      id.assign(client_id, at_pos - client_id);
      password = at_pos + 1;
    }

    bool is_self_hosted = render->config_center_->IsSelfHosted();

    if (is_self_hosted) {
      memset(&render->client_id_, 0, sizeof(render->client_id_));
      strncpy(render->client_id_, id.c_str(), sizeof(render->client_id_) - 1);
      render->client_id_[sizeof(render->client_id_) - 1] = '\0';

      memset(&render->password_saved_, 0, sizeof(render->password_saved_));
      strncpy(render->password_saved_, password.c_str(),
              sizeof(render->password_saved_) - 1);
      render->password_saved_[sizeof(render->password_saved_) - 1] = '\0';

      memset(&render->self_hosted_id_, 0, sizeof(render->self_hosted_id_));
      strncpy(render->self_hosted_id_, client_id,
              sizeof(render->self_hosted_id_) - 1);
      render->self_hosted_id_[sizeof(render->self_hosted_id_) - 1] = '\0';

      LOG_INFO("Use self-hosted client id [{}] and save to cache file", id);

      render->cd_cache_mutex_.lock();

      std::ifstream v2_file_read(render->cache_path_ + "/secure_cache_v2.enc",
                                 std::ios::binary);
      if (v2_file_read.good()) {
        v2_file_read.read(reinterpret_cast<char*>(&render->cd_cache_v2_),
                          sizeof(CDCacheV2));
        v2_file_read.close();
      } else {
        memset(&render->cd_cache_v2_, 0, sizeof(CDCacheV2));
        memset(&render->cd_cache_v2_.client_id_with_password, 0,
               sizeof(render->cd_cache_v2_.client_id_with_password));
        strncpy(render->cd_cache_v2_.client_id_with_password,
                render->client_id_with_password_,
                sizeof(render->cd_cache_v2_.client_id_with_password));
        memcpy(&render->cd_cache_v2_.key, &render->aes128_key_,
               sizeof(render->cd_cache_v2_.key));
        memcpy(&render->cd_cache_v2_.iv, &render->aes128_iv_,
               sizeof(render->cd_cache_v2_.iv));
      }

      memset(&render->cd_cache_v2_.self_hosted_id, 0,
             sizeof(render->cd_cache_v2_.self_hosted_id));
      strncpy(render->cd_cache_v2_.self_hosted_id, client_id,
              sizeof(render->cd_cache_v2_.self_hosted_id) - 1);
      render->cd_cache_v2_
          .self_hosted_id[sizeof(render->cd_cache_v2_.self_hosted_id) - 1] =
          '\0';

      memset(&render->cd_cache_v2_.client_id_with_password, 0,
             sizeof(render->cd_cache_v2_.client_id_with_password));
      strncpy(render->cd_cache_v2_.client_id_with_password,
              render->client_id_with_password_,
              sizeof(render->cd_cache_v2_.client_id_with_password));
      memcpy(&render->cd_cache_v2_.key, &render->aes128_key_,
             sizeof(render->cd_cache_v2_.key));
      memcpy(&render->cd_cache_v2_.iv, &render->aes128_iv_,
             sizeof(render->cd_cache_v2_.iv));
      std::ofstream cd_cache_v2_file(
          render->cache_path_ + "/secure_cache_v2.enc", std::ios::binary);
      if (cd_cache_v2_file) {
        cd_cache_v2_file.write(reinterpret_cast<char*>(&render->cd_cache_v2_),
                               sizeof(CDCacheV2));
        cd_cache_v2_file.close();
      }

      render->cd_cache_mutex_.unlock();
    } else {
      memset(&render->client_id_, 0, sizeof(render->client_id_));
      strncpy(render->client_id_, id.c_str(), sizeof(render->client_id_) - 1);
      render->client_id_[sizeof(render->client_id_) - 1] = '\0';

      memset(&render->password_saved_, 0, sizeof(render->password_saved_));
      strncpy(render->password_saved_, password.c_str(),
              sizeof(render->password_saved_) - 1);
      render->password_saved_[sizeof(render->password_saved_) - 1] = '\0';

      memset(&render->client_id_with_password_, 0,
             sizeof(render->client_id_with_password_));
      strncpy(render->client_id_with_password_, client_id,
              sizeof(render->client_id_with_password_) - 1);
      render
          ->client_id_with_password_[sizeof(render->client_id_with_password_) -
                                     1] = '\0';

      LOG_INFO("Use client id [{}] and save id into cache file", id);
      render->SaveSettingsIntoCacheFile();
    }
  }

  std::string remote_id(user_id, user_id_size);
  // std::shared_lock lock(render->client_properties_mutex_);
  if (render->client_properties_.find(remote_id) ==
      render->client_properties_.end()) {
    return;
  }
  auto props = render->client_properties_.find(remote_id)->second;
  if (props->traversal_mode_ != mode) {
    props->traversal_mode_ = mode;
    LOG_INFO("Net mode: [{}]", int(props->traversal_mode_));
  }

  if (!net_traffic_stats) {
    return;
  }

  // only display client side net status if connected to itself
  if (!(render->peer_reserved_ && !strstr(client_id, "C-"))) {
    props->net_traffic_stats_ = *net_traffic_stats;
  }
}
}  // namespace crossdesk
