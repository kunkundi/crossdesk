/*
 * @Author: DI JUNKUN
 * @Date: 2026-05-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINDOWS_KEY_METADATA_H_
#define _WINDOWS_KEY_METADATA_H_

#include <cstdint>

namespace crossdesk {

inline bool LookupWindowsKeyMetadataFromVk(int key_code,
                                           uint32_t* scan_code_out,
                                           bool* extended_out) {
  if (scan_code_out == nullptr || extended_out == nullptr) {
    return false;
  }

  switch (key_code) {
    case 0x21:  // Page Up
      *scan_code_out = 0x49;
      *extended_out = true;
      return true;
    case 0x22:  // Page Down
      *scan_code_out = 0x51;
      *extended_out = true;
      return true;
    case 0x23:  // End
      *scan_code_out = 0x4F;
      *extended_out = true;
      return true;
    case 0x24:  // Home
      *scan_code_out = 0x47;
      *extended_out = true;
      return true;
    case 0x25:  // Left Arrow
      *scan_code_out = 0x4B;
      *extended_out = true;
      return true;
    case 0x26:  // Up Arrow
      *scan_code_out = 0x48;
      *extended_out = true;
      return true;
    case 0x27:  // Right Arrow
      *scan_code_out = 0x4D;
      *extended_out = true;
      return true;
    case 0x28:  // Down Arrow
      *scan_code_out = 0x50;
      *extended_out = true;
      return true;
    case 0x2D:  // Insert
      *scan_code_out = 0x52;
      *extended_out = true;
      return true;
    case 0x2E:  // Delete
      *scan_code_out = 0x53;
      *extended_out = true;
      return true;
    case 0x6F:  // Numpad /
      *scan_code_out = 0x35;
      *extended_out = true;
      return true;
    case 0xA3:  // Right Ctrl
      *scan_code_out = 0x1D;
      *extended_out = true;
      return true;
    case 0xA5:  // Right Alt
      *scan_code_out = 0x38;
      *extended_out = true;
      return true;
    case 0x5B:  // Left Win
      *scan_code_out = 0x5B;
      *extended_out = true;
      return true;
    case 0x5C:  // Right Win
      *scan_code_out = 0x5C;
      *extended_out = true;
      return true;
    default:
      return false;
  }
}

}  // namespace crossdesk

#endif