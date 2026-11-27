/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-27
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HEADLESS_DISPLAY_H_
#define _HEADLESS_DISPLAY_H_

#include <CoreGraphics/CGDirectDisplay.h>

#include <chrono>

namespace crossdesk {

// Owned by the desktop device manager. All operations run on its UI thread.
class MacHeadlessDisplay {
 public:
  MacHeadlessDisplay() = default;
  ~MacHeadlessDisplay();
  MacHeadlessDisplay(const MacHeadlessDisplay&) = delete;
  MacHeadlessDisplay& operator=(const MacHeadlessDisplay&) = delete;

  static const char* NameFor(CGDirectDisplayID id);
  void Update(bool enabled, bool capturing);
  void Release();

 private:
  CFTypeRef display_ = nullptr;
  std::chrono::steady_clock::time_point next_check_{};
  std::chrono::steady_clock::time_point release_after_{};
};

}  // namespace crossdesk

#endif
