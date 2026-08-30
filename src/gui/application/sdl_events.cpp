#include "application/gui_application.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "device_controller.h"
#include "rd_log.h"

namespace crossdesk {
namespace {
int TranslateSdlKeypadScancodeToVk(const SDL_KeyboardEvent &event) {
  const bool numlock_enabled = (event.mod & SDL_KMOD_NUM) != 0;

  switch (event.scancode) {
  case SDL_SCANCODE_NUMLOCKCLEAR:
    return 0x90;
  case SDL_SCANCODE_KP_ENTER:
    return 0x0D;
  case SDL_SCANCODE_KP_0:
    if (!numlock_enabled) {
      return 0x2D;
    }
    return 0x60;
  case SDL_SCANCODE_KP_1:
    if (!numlock_enabled) {
      return 0x23;
    }
    return 0x61;
  case SDL_SCANCODE_KP_2:
    if (!numlock_enabled) {
      return 0x28;
    }
    return 0x62;
  case SDL_SCANCODE_KP_3:
    if (!numlock_enabled) {
      return 0x22;
    }
    return 0x63;
  case SDL_SCANCODE_KP_4:
    if (!numlock_enabled) {
      return 0x25;
    }
    return 0x64;
  case SDL_SCANCODE_KP_5:
    return 0x65;
  case SDL_SCANCODE_KP_6:
    if (!numlock_enabled) {
      return 0x27;
    }
    return 0x66;
  case SDL_SCANCODE_KP_7:
    if (!numlock_enabled) {
      return 0x24;
    }
    return 0x67;
  case SDL_SCANCODE_KP_8:
    if (!numlock_enabled) {
      return 0x26;
    }
    return 0x68;
  case SDL_SCANCODE_KP_9:
    if (!numlock_enabled) {
      return 0x21;
    }
    return 0x69;
  case SDL_SCANCODE_KP_PERIOD:
  case SDL_SCANCODE_KP_COMMA:
    if (!numlock_enabled) {
      return 0x2E;
    }
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

int TranslateSdlKeyboardEventToVk(const SDL_KeyboardEvent &event) {
  const int keypad_key_code = TranslateSdlKeypadScancodeToVk(event);
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

} // namespace

int GuiApplication::ProcessKeyboardEvent(const SDL_Event &event) {
  if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
    return -1;
  }

  // SDL represents key auto-repeat as additional key-down events. Forward
  // them so the fallback path has the same long-press behavior as native hooks.
  const int key_code = TranslateSdlKeyboardEventToVk(event.key);
  if (key_code < 0) {
    return 0;
  }

  return keyboard_.SendKeyCommand(key_code, event.type == SDL_EVENT_KEY_DOWN);
}

int GuiApplication::ProcessMouseEvent(const SDL_Event &event) {
  controlled_remote_id_ = "";
  RemoteAction remote_action{};

  if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
      event.button.button != SDL_BUTTON_LEFT &&
      event.button.button != SDL_BUTTON_RIGHT &&
      event.button.button != SDL_BUTTON_MIDDLE) {
    return 0;
  }

  float cursor_x = last_mouse_event.motion.x;
  float cursor_y = last_mouse_event.motion.y;

  auto normalize_cursor_to_window_space = [&](float *x, float *y) {
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

  // std::shared_lock lock(remote_sessions_mutex_);
  for (auto &it : remote_sessions_) {
    auto props = it.second;
    if (!props->control_mouse_) {
      continue;
    }

    const bool file_transfer_window_hovered =
        props->file_transfer_.file_transfer_window_hovered_;
    const bool overlay_hovered =
        props->control_bar_hovered_ || props->display_selectable_hovered_ ||
        props->shortcut_selectable_hovered_ || file_transfer_window_hovered;

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

} // namespace crossdesk
