#include "update_checker.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using crossdesk::UpdateChecker;
using crossdesk::VersionInfo;
using Status = UpdateChecker::Status;
using TimePoint = UpdateChecker::TimePoint;

bool Expect(const char* name, bool condition) {
  if (!condition) {
    std::cerr << "Failed: " << name << '\n';
  }
  return condition;
}

bool Complete(UpdateChecker& checker, TimePoint now) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (checker.Poll(now)) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return Expect("worker finishes without blocking UI polling", false);
}

bool TestAsyncAndSchedule() {
  std::promise<void> release;
  const auto gate = release.get_future().share();
  std::promise<void> started;
  auto started_future = started.get_future();
  std::atomic<int> calls{0};
  UpdateChecker checker("1.0.0", [&]() -> std::optional<VersionInfo> {
    if (++calls == 1) {
      started.set_value();
      gate.wait();
    }
    return VersionInfo{"1.0.0"};
  });
  const TimePoint now{};
  bool ok = Expect("initial state has no result", !checker.latest());
  checker.Poll(now);
  ok &= Expect("startup check starts asynchronously",
               checker.status() == Status::Checking &&
                   started_future.wait_for(5s) == std::future_status::ready);
  ok &= Expect("manual request shares running startup check",
               !checker.RequestCheck(now));
  checker.Poll(now + 8h);
  ok &= Expect("overdue timer cannot overlap active request", calls == 1);
  release.set_value();
  ok &= Complete(checker, now);
  ok &= Expect(
      "successful current version result",
      checker.status() == Status::UpToDate && !checker.update_available());
  checker.Poll(now + 6h - 1ms);
  ok &=
      Expect("no check before six hours", checker.status() == Status::UpToDate);
  checker.Poll(now + 6h);
  ok &= Complete(checker, now + 6h);
  ok &= Expect("periodic check runs once", calls == 2);
  // Simulate the process receiving no ticks while the machine is asleep.
  checker.Poll(now + 20h);
  ok &= Complete(checker, now + 20h);
  ok &= Expect("wake starts one overdue check without replaying missed checks",
               calls == 3);
  ok &= Expect("manual check bypasses six-hour interval",
               checker.RequestCheck(now + 20h + 1min));
  ok &= Complete(checker, now + 20h + 1min);
  checker.Stop();
  checker.Poll(now + 48h);
  ok &= Expect("stopped checker cannot restart",
               !checker.RequestCheck(now + 48h) && calls == 4);
  return ok;
}

bool TestRetryAndCachedResult() {
  int calls = 0;
  UpdateChecker checker(
      "1.3.5-1-20260529", [&]() -> std::optional<VersionInfo> {
        ++calls;
        if (calls <= 4 || calls == 7) {
          return std::nullopt;
        }
        if (calls == 5) {
          return VersionInfo{"1.3.5", "Patch release", "Fixes", "2026-05-29",
                             2};
        }
        if (calls == 6) {
          throw std::runtime_error("simulated network failure");
        }
        return VersionInfo{"1.3.5", "", "", "", 1};
      });
  TimePoint now{};
  checker.Poll(now);
  bool ok = Complete(checker, now);
  ok &= Expect("startup failure is not up to date",
               checker.status() == Status::Failed && !checker.latest());
  for (const auto delay : {5min, 30min, 60min, 60min}) {
    checker.Poll(now + delay - 1ms);
    ok &= Expect("failure does not retry early",
                 checker.status() == Status::Failed);
    now += delay;
    checker.Poll(now);
    ok &= Complete(checker, now);
  }
  ok &= Expect("metadata patch detects update after retries",
               checker.status() == Status::Available &&
                   checker.update_available() && calls == 5);
  checker.RequestCheck(now + 1min);
  now += 1min;
  ok &= Complete(checker, now);
  ok &= Expect("exception is a failure and preserves previous update details",
               checker.status() == Status::Failed &&
                   checker.update_available() && checker.latest() &&
                   checker.latest()->release_name == "Patch release" &&
                   checker.latest()->release_notes == "Fixes" &&
                   checker.latest()->patch == 2);
  checker.Poll(now + 5min - 1ms);
  ok &= Expect("successful check resets backoff",
               checker.status() == Status::Failed);
  now += 5min;
  checker.Poll(now);
  ok &= Complete(checker, now);
  ok &= Expect("retry is due five minutes after failure", calls == 7);
  checker.RequestCheck(now + 1min);
  ok &= Complete(checker, now + 1min);
  ok &= Expect("new successful result replaces stale metadata",
               checker.status() == Status::UpToDate &&
                   !checker.update_available() &&
                   checker.latest()->release_notes.empty() &&
                   checker.latest()->patch == 1);
  return ok;
}

bool TestStopDuringFetch() {
  std::atomic<bool> finished{false};
  UpdateChecker checker("1.0.0", [&]() -> std::optional<VersionInfo> {
    std::this_thread::sleep_for(10ms);
    finished = true;
    return VersionInfo{"2.0.0"};
  });
  checker.RequestCheck();
  checker.Stop();
  return Expect("shutdown joins the worker and discards pending UI results",
                finished && !checker.Poll() && !checker.latest() &&
                    !checker.RequestCheck());
}

}  // namespace

int main() {
  bool ok = TestAsyncAndSchedule();
  ok &= TestRetryAndCachedResult();
  ok &= TestStopDuringFetch();
  return ok ? 0 : 1;
}
