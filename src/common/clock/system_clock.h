/*
 * @Author: DI JUNKUN
 * @Date: 2025-02-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SYSTEM_CLOCK_H_
#define _SYSTEM_CLOCK_H_

#include <cstdint>
#include <memory>

static const int64_t kNtpEpochOffset = 2208988800LL;

class SystemClock {
 public:
  SystemClock() = default;
  ~SystemClock() = default;

  int64_t CurrentTime();
  int64_t CurrentTimeUs();
  int64_t CurrentTimeMs();
  int64_t CurrentTimeNs();

  int64_t CurrentNtpTime();
  int64_t CurrentNtpTimeMs();

  int64_t CurrentUtcTime();
  int64_t CurrentUtcTimeMs();
  int64_t CurrentUtcTimeUs();
  int64_t CurrentUtcTimeNs();

  int64_t ConvertToNtpTime(int64_t time_us);

  int64_t NtpToUtc(int64_t ntp_time);

  int64_t CurrentNtpInMilliseconds() { return CurrentNtpTimeMs(); }
};

#endif