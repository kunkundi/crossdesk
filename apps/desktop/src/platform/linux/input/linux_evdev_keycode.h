#ifndef _LINUX_EVDEV_KEYCODE_H_
#define _LINUX_EVDEV_KEYCODE_H_

#include <linux/input-event-codes.h>

#include <cstdint>

namespace crossdesk {

inline int LinuxEvdevKeycodeFromWindowsScanCode(uint32_t scan_code,
                                                bool extended) {
  const uint32_t base_scan_code = scan_code & 0xFFu;
  if (base_scan_code == 0) {
    return -1;
  }

  if (extended) {
    switch (base_scan_code) {
      case 0x1C:
        return KEY_KPENTER;
      case 0x1D:
        return KEY_RIGHTCTRL;
      case 0x35:
        return KEY_KPSLASH;
      case 0x38:
        return KEY_RIGHTALT;
      case 0x47:
        return KEY_HOME;
      case 0x48:
        return KEY_UP;
      case 0x49:
        return KEY_PAGEUP;
      case 0x4B:
        return KEY_LEFT;
      case 0x4D:
        return KEY_RIGHT;
      case 0x4F:
        return KEY_END;
      case 0x50:
        return KEY_DOWN;
      case 0x51:
        return KEY_PAGEDOWN;
      case 0x52:
        return KEY_INSERT;
      case 0x53:
        return KEY_DELETE;
      case 0x5B:
        return KEY_LEFTMETA;
      case 0x5C:
        return KEY_RIGHTMETA;
      default:
        return -1;
    }
  }

  // For the common PC set-1 keys, Linux evdev key codes intentionally line up
  // with the low byte of the Windows scan code.
  if ((base_scan_code >= 0x01 && base_scan_code <= 0x53) ||
      base_scan_code == 0x56 || base_scan_code == 0x57 ||
      base_scan_code == 0x58) {
    return static_cast<int>(base_scan_code);
  }

  return -1;
}

inline int LinuxEvdevKeycodeFromWindowsVk(int key_code) {
  switch (key_code) {
    case 0x08:
      return KEY_BACKSPACE;
    case 0x09:
      return KEY_TAB;
    case 0x0D:
      return KEY_ENTER;
    case 0x10:
    case 0xA0:
      return KEY_LEFTSHIFT;
    case 0x11:
    case 0xA2:
      return KEY_LEFTCTRL;
    case 0x12:
    case 0xA4:
      return KEY_LEFTALT;
    case 0x13:
      return KEY_PAUSE;
    case 0x14:
      return KEY_CAPSLOCK;
    case 0x1B:
      return KEY_ESC;
    case 0x20:
      return KEY_SPACE;
    case 0x21:
      return KEY_PAGEUP;
    case 0x22:
      return KEY_PAGEDOWN;
    case 0x23:
      return KEY_END;
    case 0x24:
      return KEY_HOME;
    case 0x25:
      return KEY_LEFT;
    case 0x26:
      return KEY_UP;
    case 0x27:
      return KEY_RIGHT;
    case 0x28:
      return KEY_DOWN;
    case 0x2C:
      return KEY_SYSRQ;
    case 0x2D:
      return KEY_INSERT;
    case 0x2E:
      return KEY_DELETE;
    case 0x30:
      return KEY_0;
    case 0x31:
      return KEY_1;
    case 0x32:
      return KEY_2;
    case 0x33:
      return KEY_3;
    case 0x34:
      return KEY_4;
    case 0x35:
      return KEY_5;
    case 0x36:
      return KEY_6;
    case 0x37:
      return KEY_7;
    case 0x38:
      return KEY_8;
    case 0x39:
      return KEY_9;
    case 0x41:
      return KEY_A;
    case 0x42:
      return KEY_B;
    case 0x43:
      return KEY_C;
    case 0x44:
      return KEY_D;
    case 0x45:
      return KEY_E;
    case 0x46:
      return KEY_F;
    case 0x47:
      return KEY_G;
    case 0x48:
      return KEY_H;
    case 0x49:
      return KEY_I;
    case 0x4A:
      return KEY_J;
    case 0x4B:
      return KEY_K;
    case 0x4C:
      return KEY_L;
    case 0x4D:
      return KEY_M;
    case 0x4E:
      return KEY_N;
    case 0x4F:
      return KEY_O;
    case 0x50:
      return KEY_P;
    case 0x51:
      return KEY_Q;
    case 0x52:
      return KEY_R;
    case 0x53:
      return KEY_S;
    case 0x54:
      return KEY_T;
    case 0x55:
      return KEY_U;
    case 0x56:
      return KEY_V;
    case 0x57:
      return KEY_W;
    case 0x58:
      return KEY_X;
    case 0x59:
      return KEY_Y;
    case 0x5A:
      return KEY_Z;
    case 0x5B:
      return KEY_LEFTMETA;
    case 0x5C:
      return KEY_RIGHTMETA;
    case 0x60:
      return KEY_KP0;
    case 0x61:
      return KEY_KP1;
    case 0x62:
      return KEY_KP2;
    case 0x63:
      return KEY_KP3;
    case 0x64:
      return KEY_KP4;
    case 0x65:
      return KEY_KP5;
    case 0x66:
      return KEY_KP6;
    case 0x67:
      return KEY_KP7;
    case 0x68:
      return KEY_KP8;
    case 0x69:
      return KEY_KP9;
    case 0x6A:
      return KEY_KPASTERISK;
    case 0x6B:
      return KEY_KPPLUS;
    case 0x6D:
      return KEY_KPMINUS;
    case 0x6E:
      return KEY_KPDOT;
    case 0x6F:
      return KEY_KPSLASH;
    case 0x70:
      return KEY_F1;
    case 0x71:
      return KEY_F2;
    case 0x72:
      return KEY_F3;
    case 0x73:
      return KEY_F4;
    case 0x74:
      return KEY_F5;
    case 0x75:
      return KEY_F6;
    case 0x76:
      return KEY_F7;
    case 0x77:
      return KEY_F8;
    case 0x78:
      return KEY_F9;
    case 0x79:
      return KEY_F10;
    case 0x7A:
      return KEY_F11;
    case 0x7B:
      return KEY_F12;
    case 0x90:
      return KEY_NUMLOCK;
    case 0x91:
      return KEY_SCROLLLOCK;
    case 0xA1:
      return KEY_RIGHTSHIFT;
    case 0xA3:
      return KEY_RIGHTCTRL;
    case 0xA5:
      return KEY_RIGHTALT;
    case 0xBA:
      return KEY_SEMICOLON;
    case 0xBB:
      return KEY_EQUAL;
    case 0xBC:
      return KEY_COMMA;
    case 0xBD:
      return KEY_MINUS;
    case 0xBE:
      return KEY_DOT;
    case 0xBF:
      return KEY_SLASH;
    case 0xC0:
      return KEY_GRAVE;
    case 0xDB:
      return KEY_LEFTBRACE;
    case 0xDC:
      return KEY_BACKSLASH;
    case 0xDD:
      return KEY_RIGHTBRACE;
    case 0xDE:
      return KEY_APOSTROPHE;
    default:
      return -1;
  }
}

inline int ResolveLinuxEvdevKeycodeFromWindowsKey(int key_code,
                                                  uint32_t scan_code,
                                                  bool extended) {
  const int scan_keycode =
      LinuxEvdevKeycodeFromWindowsScanCode(scan_code, extended);
  if (scan_keycode >= 0) {
    return scan_keycode;
  }

  return LinuxEvdevKeycodeFromWindowsVk(key_code);
}

}  // namespace crossdesk

#endif
