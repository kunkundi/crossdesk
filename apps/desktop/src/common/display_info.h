/*
 * @Author: DI JUNKUN
 * @Date: 2025-05-15
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_INFO_H_
#define _DISPLAY_INFO_H_

#include <string>

namespace crossdesk {

class DisplayInfo {
 public:
  DisplayInfo(std::string name, int left, int top, int right, int bottom)
      : name(name), left(left), top(top), right(right), bottom(bottom) {
    width = right - left;
    height = bottom - top;
  }
  DisplayInfo(void* handle, std::string name, bool is_primary, int left,
              int top, int right, int bottom)
      : handle(handle),
        name(name),
        is_primary(is_primary),
        left(left),
        top(top),
        right(right),
        bottom(bottom) {
    width = right - left;
    height = bottom - top;
  }
  ~DisplayInfo() {}

  void* handle = nullptr;
  std::string name = "";
  bool is_primary = false;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  int width = 0;
  int height = 0;
};
}  // namespace crossdesk
#endif