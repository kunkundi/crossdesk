#include "system_clock.h"

#include <time.h>

#include <cstdint>
#include <limits>

#if defined(__POSIX__)
#include <sys/time.h>
#endif
#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#endif

int64_t ConvertToNtpTime(int64_t time_us) {
  constexpr int64_t kMicrosecondsPerSecond = 1000000;
  constexpr uint64_t kNtpFractionalUnit = 0x100000000;  // 2^32
  uint32_t seconds = static_cast<uint32_t>(time_us / kMicrosecondsPerSecond);
  uint32_t fractions =
      static_cast<uint32_t>((time_us % kMicrosecondsPerSecond) *
                            kNtpFractionalUnit / kMicrosecondsPerSecond);

  return seconds * kNtpFractionalUnit + fractions;
}

int64_t SystemClock::CurrentTimeNs() {
  int64_t ticks = -1;  // Default to error case

#if defined(__APPLE__)
  static mach_timebase_info_data_t timebase;
  if (timebase.denom == 0 && mach_timebase_info(&timebase) != KERN_SUCCESS) {
    return -1;  // Error case for macOS timebase info retrieval
  }
  ticks = static_cast<int64_t>(mach_absolute_time() * timebase.numer) /
          timebase.denom;

#elif defined(__POSIX__)
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return -1;  // Error case for POSIX clock retrieval
  }
  ticks = static_cast<int64_t>(ts.tv_sec) * kNumNanosecsPerSec +
          static_cast<int64_t>(ts.tv_nsec);

#elif defined(_WIN32)
  static volatile LONG last_timegettime = 0;
  static volatile int64_t num_wrap_timegettime = 0;
  volatile LONG* last_timegettime_ptr = &last_timegettime;

  DWORD now = timeGetTime();
  DWORD old = InterlockedExchange(last_timegettime_ptr, now);

  if (now < old) {
    // Handle wraparound (when timeGetTime() wraps around after ~49.7 days)
    if (old > 0xf0000000 && now < 0x0fffffff) {
      num_wrap_timegettime++;
    }
  }

  // Convert milliseconds to nanoseconds and add wraparound offset
  ticks = static_cast<int64_t>(now) + (num_wrap_timegettime << 32);
  ticks *= 1000000;
#endif

  return ticks;
}

int64_t SystemClock::CurrentTimeUs() { return CurrentTimeNs() / 1000LL; }

int64_t SystemClock::CurrentTimeMs() { return CurrentTimeNs() / 1000000LL; }

int64_t SystemClock::CurrentTime() { return CurrentTimeNs() / 1000000000LL; }

int64_t SystemClock::CurrentNtpTime() {
  return ConvertToNtpTime(CurrentTimeNs());
}

int64_t SystemClock::CurrentUtcTimeNs() {
#if defined(__POSIX__)
  struct timeval time;
  gettimeofday(&time, nullptr);
  return (static_cast<int64_t>(time.tv_sec) * 1000000000 + time.tv_usec * 1000);
#elif defined(_WIN32)
  FILETIME file_time;
  GetSystemTimeAsFileTime(&file_time);
  int64_t file_time_100ns =
      ((int64_t)file_time.dwHighDateTime << 32) | file_time.dwLowDateTime;
  constexpr int64_t kUnixEpochFileTimeOffsetIn100ns = 116444736000000000LL;
  return (file_time_100ns - kUnixEpochFileTimeOffsetIn100ns) * 100;
#elif defined(__APPLE__)
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return -1;  // Error case for macOS clock retrieval
  }
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
#endif
  return 0;
}

int64_t SystemClock::CurrentUtcTimeUs() { return CurrentUtcTimeNs() / 1000LL; }

int64_t SystemClock::CurrentUtcTimeMs() {
  return CurrentUtcTimeNs() / 1000000LL;
}

int64_t SystemClock::CurrentUtcTime() {
  return CurrentUtcTimeNs() / 1000000000LL;
}
