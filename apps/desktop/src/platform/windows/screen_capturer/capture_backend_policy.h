/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CAPTURE_BACKEND_POLICY_H_
#define _CAPTURE_BACKEND_POLICY_H_

#include <array>

#include "screen_capture_method.h"

namespace crossdesk {

inline constexpr std::array<ScreenCaptureMethod, 3> WindowsCaptureBackendOrder() {
  // One policy for physical/virtual outputs, headless and secure desktops.
  return {ScreenCaptureMethod::Dxgi, ScreenCaptureMethod::Wgc,
          ScreenCaptureMethod::Gdi};
}

}  // namespace crossdesk

#endif
