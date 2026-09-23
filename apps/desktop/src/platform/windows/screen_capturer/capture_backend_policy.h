/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CAPTURE_BACKEND_POLICY_H_
#define _CAPTURE_BACKEND_POLICY_H_

#include <vector>

#include "screen_capture_method.h"

namespace crossdesk {

inline std::vector<ScreenCaptureMethod> WindowsCaptureBackendOrder(
    ScreenCaptureMethod requested, bool headless, bool usbmmidd) {
  if (requested != ScreenCaptureMethod::Auto) return {requested};
  if (headless) return {ScreenCaptureMethod::Gdi};
  // usbmmidd may put its software cursor directly into a DXGI/GDI image.
  // WGC can exclude it at capture time. Keep WGC for both cursor modes so a
  // native client joining/leaving never needs to inherit an embedded cursor.
  // Do not silently fall back to a backend that cannot exclude that cursor.
  if (usbmmidd) return {ScreenCaptureMethod::Wgc};
  return {ScreenCaptureMethod::Dxgi, ScreenCaptureMethod::Wgc,
          ScreenCaptureMethod::Gdi};
}

}  // namespace crossdesk

#endif