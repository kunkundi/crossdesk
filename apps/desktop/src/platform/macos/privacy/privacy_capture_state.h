/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-13
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */
#ifndef _PRIVACY_CAPTURE_STATE_H_
#define _PRIVACY_CAPTURE_STATE_H_

#include <chrono>
#include <cstdint>
#include <vector>

namespace crossdesk {
struct MacPrivacyCaptureState {
  uint64_t revision = 0;
  uint64_t applied_revision = 0;
  std::chrono::steady_clock::time_point filter_deadline{};
  std::vector<uint32_t> excluded_windows;
  uint32_t cursor_window = 0;
};
MacPrivacyCaptureState GetMacPrivacyCaptureState();
void InvalidateMacPrivacyCaptureFilter();
bool AcknowledgeMacPrivacyCapture(uint64_t revision);
}

#endif
