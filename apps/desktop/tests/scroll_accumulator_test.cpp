#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include "scroll_accumulator.h"

namespace {

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(
            current /
            "apps/desktop/src/gui/features/input/scroll_accumulator.h")) {
      return current;
    }
    current = current.parent_path();
  }
  return {};
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  std::string contents = stream.str();
  contents.erase(std::remove(contents.begin(), contents.end(), '\r'),
                 contents.end());
  return contents;
}

bool Expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << "FAIL: " << message << '\n';
  return false;
}

bool ExpectContains(const char* name, const std::string& value,
                    const std::string& expected) {
  if (value.find(expected) != std::string::npos) {
    return true;
  }
  std::cerr << name << " missing expected text: " << expected << '\n';
  return false;
}

// Mirrors winit 0.30.13 (src/platform_impl/windows/event_loop.rs) + Slint's
// winit backend (60 logical pixels per LineDelta unit): WM_MOUSEWHEEL keeps
// GET_WHEEL_DELTA_WPARAM's sign, whereas WM_MOUSEHWHEEL negates it.
float Win32WheelDeltaToSlintPixels(bool vertical, short wheel_delta) {
  const float lines = static_cast<float>(wheel_delta) / 120.0f;
  return (vertical ? lines : -lines) * 60.0f;
}

bool TestHorizontalTiltDirectionMatchesWin32() {
  using Clock = crossdesk::ScrollAccumulator::Clock;
  const Clock::time_point t0{};
  crossdesk::ScrollAccumulator scroll;
  bool ok = true;

  // Win32 WM_MOUSEHWHEEL: +120 = tilt right, -120 = tilt left.
  // Windows SendInput(MOUSEEVENTF_HWHEEL) expects +120 = right, -120 = left,
  // so Consume() must emit +1 for right tilt and -1 for left tilt.
  const float right_tilt_dx = Win32WheelDeltaToSlintPixels(false, 120);
  const float left_tilt_dx = Win32WheelDeltaToSlintPixels(false, -120);
  ok &= Expect(right_tilt_dx == -60.0f && left_tilt_dx == 60.0f,
               "Slint/winit horizontal delta test fixture mismatch");
  ok &= Expect(scroll.Consume(false, right_tilt_dx, 100, t0) == 1,
               "tilting wheel right (+120 WM_MOUSEHWHEEL) should emit +1");
  ok &= Expect(
      scroll.Consume(false, left_tilt_dx, 100,
                     t0 + std::chrono::milliseconds(250)) == -1,
      "tilting wheel left (-120 WM_MOUSEHWHEEL) should emit -1");

  // Vertical wheel direction remains +1 = up, -1 = down.
  const float wheel_up_dy = Win32WheelDeltaToSlintPixels(true, 120);
  const float wheel_down_dy = Win32WheelDeltaToSlintPixels(true, -120);
  ok &= Expect(scroll.Consume(true, wheel_up_dy, 100, t0) == 1,
               "scrolling wheel up (+120 WM_MOUSEWHEEL) should emit +1");
  ok &= Expect(
      scroll.Consume(true, wheel_down_dy, 100,
                     t0 + std::chrono::milliseconds(250)) == -1,
      "scrolling wheel down (-120 WM_MOUSEWHEEL) should emit -1");
  return ok;
}

bool TestHighResolutionAndSensitivityBehavior() {
  using Clock = crossdesk::ScrollAccumulator::Clock;
  const Clock::time_point t0{};
  bool ok = true;

#if defined(_WIN32)
  {
    // High-resolution right tilt (four +30 WM_MOUSEHWHEEL quarters -> -15 px).
    crossdesk::ScrollAccumulator scroll;
    const float quarter_right_dx = Win32WheelDeltaToSlintPixels(false, 30);
    ok &= Expect(scroll.Consume(false, quarter_right_dx, 100, t0) == 0,
                 "1/4 right tilt should accumulate");
    ok &= Expect(scroll.Consume(false, quarter_right_dx, 100,
                                t0 + std::chrono::milliseconds(20)) == 0,
                 "2/4 right tilt should accumulate");
    ok &= Expect(scroll.Consume(false, quarter_right_dx, 100,
                                t0 + std::chrono::milliseconds(40)) == 0,
                 "3/4 right tilt should accumulate");
    ok &= Expect(scroll.Consume(false, quarter_right_dx, 100,
                                t0 + std::chrono::milliseconds(60)) == 1,
                 "4/4 right tilt should emit +1 step");

    // Reversing to left tilt mid-gesture must discard opposite remainder.
    const float half_right_dx = Win32WheelDeltaToSlintPixels(false, 60);
    const float half_left_dx = Win32WheelDeltaToSlintPixels(false, -60);
    ok &= Expect(scroll.Consume(false, half_right_dx, 100,
                                t0 + std::chrono::milliseconds(80)) == 0,
                 "half right tilt should stay buffered");
    ok &= Expect(scroll.Consume(false, half_left_dx, 100,
                                t0 + std::chrono::milliseconds(100)) == 0,
                 "first half left tilt after reversal should reset remainder");
    ok &= Expect(scroll.Consume(false, half_left_dx, 100,
                                t0 + std::chrono::milliseconds(120)) == -1,
                 "second half left tilt after reversal should emit -1 step");
  }
#endif

  {
    // Low sensitivity (50%) carries whole notches across pauses.
    crossdesk::ScrollAccumulator scroll;
    const float right_tilt_dx = Win32WheelDeltaToSlintPixels(false, 120);
    ok &= Expect(scroll.Consume(false, right_tilt_dx, 50, t0) == 0,
                 "first notch at 50% sensitivity should buffer half step");
    ok &= Expect(scroll.Consume(false, right_tilt_dx, 50,
                                t0 + std::chrono::milliseconds(500)) == 1,
                 "second notch at 50% sensitivity should emit +1 step");
  }

  {
    crossdesk::ScrollAccumulator scroll;
    ok &= Expect(scroll.Consume(false, 0.0f, 100, t0) == 0,
                 "zero horizontal delta should emit 0");
    ok &= Expect(
        scroll.Consume(false, std::numeric_limits<float>::quiet_NaN(), 100,
                       t0) == 0,
        "NaN horizontal delta should emit 0");
    ok &= Expect(
        scroll.Consume(false, std::numeric_limits<float>::infinity(), 100,
                       t0) == 0,
        "infinite horizontal delta should emit 0");
  }
  return ok;
}

bool TestHostHorizontalWheelContracts(const std::filesystem::path& repo_root) {
  const std::string win_mouse = ReadFile(
      repo_root /
      "apps/desktop/src/platform/windows/input/mouse/mouse_controller.cpp");
  const std::string win_helper = ReadFile(
      repo_root /
      "apps/desktop/src/platform/windows/service/session_helper_main.cpp");
  const std::string mac_mouse = ReadFile(
      repo_root /
      "apps/desktop/src/platform/macos/input/mouse/mouse_controller.cpp");
  const std::string linux_mouse = ReadFile(
      repo_root /
      "apps/desktop/src/platform/linux/input/mouse/mouse_controller.cpp");

  bool ok = true;
  ok &= ExpectContains("windows/mouse_controller.cpp", win_mouse,
                       "case MouseFlag::wheel_horizontal:\n"
                       "      ip.mi.dwFlags = MOUSEEVENTF_HWHEEL;\n"
                       "      ip.mi.mouseData = remote_action.m.s * 120;");
  ok &= ExpectContains("windows/session_helper_main.cpp", win_helper,
                       "action_input.mi.dwFlags = MOUSEEVENTF_HWHEEL;\n"
                       "      action_input.mi.mouseData = request.wheel * 120;");
  ok &= ExpectContains(
      "macos/mouse_controller.cpp", mac_mouse,
      "case MouseFlag::wheel_horizontal:\n"
      "      // CoreGraphics wheel2 uses positive values for scrolling left.\n"
      "      mouse_event = CGEventCreateScrollWheelEvent(\n"
      "          NULL, kCGScrollEventUnitLine, 2, 0,\n"
      "          -std::clamp(remote_action.m.s, -1200, 1200));");
  ok &= ExpectContains(
      "linux/mouse_controller.cpp", linux_mouse,
      "case MouseFlag::wheel_horizontal: {\n"
      "          // X11 maps button 6 to scroll left and button 7 to scroll right.\n"
      "          if (remote_action.m.s > 0) {\n"
      "            SimulateMouseWheel(7, (std::min)(remote_action.m.s, 1200));\n"
      "          } else if (remote_action.m.s < 0) {\n"
      "            SimulateMouseWheel(6, -(std::max)(remote_action.m.s, -1200));\n"
      "          }");
  return ok;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  bool ok = true;
  ok &= TestHorizontalTiltDirectionMatchesWin32();
  ok &= TestHighResolutionAndSensitivityBehavior();
  ok &= TestHostHorizontalWheelContracts(repo_root);
  return ok ? 0 : 1;
}
