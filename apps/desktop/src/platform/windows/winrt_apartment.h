/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WINRT_APARTMENT_H_
#define _WINRT_APARTMENT_H_

#include <roapi.h>

#include "rd_log.h"

namespace crossdesk {

// Keep WinRT initialized for the lifetime of the thread's capture objects.
// An existing STA is valid too; balance only our successful initialization.
class ScopedWinRtApartment {
 public:
  ScopedWinRtApartment() : result_(RoInitialize(RO_INIT_MULTITHREADED)) {
    if (!ready())
      LOG_ERROR("WinRT initialization failed, hr=0x{:08X}, thread={}",
                static_cast<uint32_t>(result_), GetCurrentThreadId());
  }
  ~ScopedWinRtApartment() {
    if (SUCCEEDED(result_)) RoUninitialize();
  }
  ScopedWinRtApartment(const ScopedWinRtApartment&) = delete;
  ScopedWinRtApartment& operator=(const ScopedWinRtApartment&) = delete;
  bool ready() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }

 private:
  HRESULT result_;
};

}  // namespace crossdesk

#endif