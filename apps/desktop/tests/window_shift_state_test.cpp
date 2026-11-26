#include <iostream>
#include <set>
#include <utility>
#include <vector>

#include "keyboard_state.h"
#include "stream_keyboard_focus.h"
#include "window_shift_state.h"
#ifdef __linux__
#include "linux_evdev_keycode.h"
#endif

namespace {

using Event = std::pair<int, bool>;

struct Harness {
  crossdesk::WindowShiftState state;
  std::set<int> pressed;
  std::vector<Event> events;

  crossdesk::WindowShiftState::Held CurrentHeld() const {
    return {
        pressed.count(crossdesk::WindowShiftState::kLeft) != 0,
        pressed.count(crossdesk::WindowShiftState::kRight) != 0,
        pressed.count(crossdesk::WindowShiftState::kLeftControl) != 0,
        pressed.count(crossdesk::WindowShiftState::kRightControl) != 0,
        pressed.count(crossdesk::WindowShiftState::kLeftAlt) != 0,
        pressed.count(crossdesk::WindowShiftState::kRightAlt) != 0,
        pressed.count(crossdesk::WindowShiftState::kLeftMeta) != 0,
        pressed.count(crossdesk::WindowShiftState::kRightMeta) != 0,
    };
  }

  void Send(int key, bool down) {
    events.emplace_back(key, down);
    if (down) {
      pressed.insert(key);
    } else {
      pressed.erase(key);
    }
  }

  void Forward(int key, bool down,
               crossdesk::WindowShiftState::Modifiers modifiers) {
    state.Forward(key, down, modifiers, CurrentHeld(),
                  [this](int k, bool d) { Send(k, d); });
  }

  void Reconcile(crossdesk::WindowShiftState::Modifiers live) {
    state.ReconcileModifiers(live, CurrentHeld(),
                             [this](int k, bool d) { Send(k, d); });
  }

  void ReleaseWindowKey(int key) {
    state.ReleaseWindowKey(key, pressed.count(key) != 0,
                           [this](int k, bool d) { Send(k, d); });
  }

  void ReleaseInferred() {
    state.ReleaseInferred([this](int k, bool d) { Send(k, d); });
  }
};

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

bool TestInferredShiftAndRealTakeover() {
  bool ok = true;
  Harness h;

  // Focus arrived while Shift was already held: typing 'A' infers left Shift.
  h.Forward('A', true, {false, false, true, false});
  ok &= Expect(h.events == std::vector<Event>{
                               {crossdesk::WindowShiftState::kLeft, true},
                               {'A', true},
                           },
               "character press with shift=true must infer left Shift");
  h.Forward('A', false, {false, false, true, false});

  // Real left Shift press takes over without emitting a duplicate key-down.
  h.Forward(crossdesk::WindowShiftState::kLeft, true,
            {false, false, true, false});
  ok &= Expect(h.events.size() == 3,
               "real left Shift press must adopt inferred left Shift");

  // Releasing real left Shift releases remote Shift cleanly.
  h.Forward(crossdesk::WindowShiftState::kLeft, false,
            {false, false, false, false});
  ok &= Expect(h.pressed.empty(),
               "releasing real left Shift must leave no pressed keys");
  return ok;
}

bool TestKeyReleaseNeverSynthesizesShift() {
  bool ok = true;
  Harness h;

  // User types Shift + A, releases Shift slightly before A, and Slint/XKB
  // still reports shift=true on A's key-released event (Issue #266 Symptom 1).
  h.Forward(crossdesk::WindowShiftState::kLeft, true,
            {false, false, true, false});
  h.Forward('A', true, {false, false, true, false});
  h.Forward(crossdesk::WindowShiftState::kLeft, false,
            {false, false, false, false});
  h.events.clear();

  h.Forward('A', false, {false, false, true, false});
  ok &= Expect(h.events == std::vector<Event>{{'A', false}},
               "key-released with lagged shift=true must never press Shift");
  ok &= Expect(h.pressed.empty(),
               "no modifier may remain pressed before subsequent mouse click");
  return ok;
}

bool TestModifierAndLockChordsDoNotInferShift() {
  bool ok = true;
  Harness h;

  // Alt+Shift / Ctrl+Shift IME/layout switches and CapsLock must not infer
  // provisional Shift when Shift key-down was not delivered to the window.
  h.Forward(crossdesk::WindowShiftState::kLeftAlt, true,
            {false, true, true, false});
  h.Forward(crossdesk::WindowShiftState::kLeftAlt, false,
            {false, false, true, false});
  h.Forward(crossdesk::WindowShiftState::kLeftControl, true,
            {true, false, true, false});
  h.Forward(crossdesk::WindowShiftState::kLeftControl, false,
            {false, false, true, false});
  h.Forward(0x14, true, {false, false, true, false});   // VK_CAPITAL
  h.Forward(0x14, false, {false, false, true, false});  // VK_CAPITAL

  ok &= Expect(h.pressed.count(crossdesk::WindowShiftState::kLeft) == 0 &&
                   h.pressed.count(crossdesk::WindowShiftState::kRight) == 0,
               "modifier/lock keys must not synthesize inferred Shift");
  return ok;
}

bool TestMissedModifiersReleasedBeforeEscape() {
  bool ok = true;
  constexpr int kEscape = 0x1B;

  // Issue #266 Symptom 3: Ctrl/Alt/Meta/Shift key-up lost during rapid window
  // switching would turn Esc into Ctrl+Esc (Windows Start Menu / Win).
  Harness h;
  h.Forward(crossdesk::WindowShiftState::kLeftControl, true,
            {true, false, false, false});
  h.Forward(crossdesk::WindowShiftState::kRightControl, true,
            {true, false, false, false});
  h.Forward(crossdesk::WindowShiftState::kLeftAlt, true,
            {true, true, false, false});
  h.Forward(crossdesk::WindowShiftState::kLeftMeta, true,
            {true, true, false, true});
  h.Forward(crossdesk::WindowShiftState::kRight, true,
            {true, true, true, true});
  h.events.clear();

  // User returns to the stream and presses Esc with no modifiers held.
  h.Forward(kEscape, true, {false, false, false, false});
  h.Forward(kEscape, false, {false, false, false, false});

  ok &= Expect(h.pressed.empty(),
               "all stale modifiers and Esc must be released");
  ok &= Expect(h.events.size() == 7 &&
                   h.events[5] == Event{kEscape, true} &&
                   h.events[6] == Event{kEscape, false},
               "stale modifiers must be released before Esc key-down");
  return ok;
}

bool TestAltGrChordPreservedOnKeyEvents() {
  bool ok = true;
  Harness h;

  // Windows/XKB AltGr can hold kRightAlt (with or without synthetic
  // kLeftControl) while Slint reports control=false and alt=false.
  h.Forward(crossdesk::WindowShiftState::kLeftControl, true,
            {true, false, false, false});
  h.Forward(crossdesk::WindowShiftState::kRightAlt, true,
            {false, false, false, false});
  h.events.clear();

  h.Forward('E', true, {false, false, false, false});
  h.Forward('E', false, {false, false, false, false});

  ok &= Expect(h.pressed.count(crossdesk::WindowShiftState::kLeftControl) == 1 &&
                   h.pressed.count(crossdesk::WindowShiftState::kRightAlt) == 1,
               "active AltGr chord must not be stripped by ordinary key press");

  // Authoritative OS pointer reconciliation with all modifiers up clears them.
  h.Reconcile({false, false, false, false});
  ok &= Expect(h.pressed.empty(),
               "pointer reconciliation must clear released AltGr modifiers");
  return ok;
}

bool TestPointerReconciliationPreventsAccidentalShiftClick() {
  bool ok = true;
  Harness h;

  // Inferred Shift left active before clicking in remote desktop.
  h.Forward('A', true, {false, false, true, false});
  h.Forward('A', false, {false, false, true, false});
  ok &= Expect(h.pressed.count(crossdesk::WindowShiftState::kLeft) == 1,
               "inferred Shift initially active after shifted character");

  // Clicking with physical Shift released must release Shift before click.
  h.Reconcile({false, false, false, false});
  ok &= Expect(h.pressed.empty(),
               "pointer reconciliation must release Shift before click");

  // Genuine Shift+Click (physical Shift held) must preserve Shift.
  h.Forward(crossdesk::WindowShiftState::kLeft, true,
            {false, false, true, false});
  h.Reconcile({false, false, true, false});
  ok &= Expect(h.pressed.count(crossdesk::WindowShiftState::kLeft) == 1,
               "genuine Shift+Click must keep Shift pressed");
  return ok;
}

bool TestNativeCapturerHandoffReleasesWindowKeysOnce() {
  bool ok = true;
  Harness h;

  // Key pressed via Slint window event right before native hook starts.
  h.Forward(crossdesk::WindowShiftState::kLeftControl, true,
            {true, false, false, false});
  h.Forward('C', true, {true, false, false, false});
  ok &= Expect(h.state.HasWindowKey(crossdesk::WindowShiftState::kLeftControl) &&
                   h.state.HasWindowKey('C'),
               "window-forwarded presses must be tracked for handoff");

  // Native capturer observes 'C' directly and takes ownership of it.
  h.state.ClearWindowKey('C');
  h.Send('C', false);

  // Late Slint key-released arrives for both keys after native capture started:
  // 'C' must not double-release, while kLeftControl is cleanly released once.
  h.events.clear();
  h.ReleaseWindowKey('C');
  h.ReleaseWindowKey(crossdesk::WindowShiftState::kLeftControl);
  h.ReleaseWindowKey(crossdesk::WindowShiftState::kLeftControl);

  ok &= Expect(h.events == std::vector<Event>{
                               {crossdesk::WindowShiftState::kLeftControl,
                                false}},
               "handoff must release only unowned window key once");
  ok &= Expect(h.pressed.empty(), "all keys released after handoff");
  return ok;
}

bool TestStreamKeyboardFocusTransitions() {
  bool ok = true;
  bool focused = true;
  std::vector<bool> preserve_calls;

  crossdesk::UpdateStreamKeyboardFocus(
      false, false, focused, [&](bool preserve) { preserve_calls.push_back(preserve); });
  ok &= Expect(!focused && preserve_calls == std::vector<bool>{false},
               "window focus loss must release full remote control");

  // Redundant focus-loss notification while already unfocused is ignored.
  crossdesk::UpdateStreamKeyboardFocus(
      false, false, focused, [&](bool preserve) { preserve_calls.push_back(preserve); });
  ok &= Expect(preserve_calls.size() == 1,
               "redundant focus loss must not release twice");

  focused = true;
  crossdesk::UpdateStreamKeyboardFocus(
      false, true, focused, [&](bool preserve) { preserve_calls.push_back(preserve); });
  ok &= Expect(!focused && preserve_calls == std::vector<bool>{false, true},
               "local menu/modal focus must preserve local UI state");

#ifdef __linux__
  ok &= Expect(
      crossdesk::LinuxEvdevKeycodeFromWindowsScanCode(0x36, true) ==
          KEY_RIGHTSHIFT,
      "extended scan code 0x36 must resolve to KEY_RIGHTSHIFT");
#endif
  return ok;
}

}  // namespace

int main() {
  bool ok = true;
  ok &= TestInferredShiftAndRealTakeover();
  ok &= TestKeyReleaseNeverSynthesizesShift();
  ok &= TestModifierAndLockChordsDoNotInferShift();
  ok &= TestMissedModifiersReleasedBeforeEscape();
  ok &= TestAltGrChordPreservedOnKeyEvents();
  ok &= TestPointerReconciliationPreventsAccidentalShiftClick();
  ok &= TestNativeCapturerHandoffReleasesWindowKeysOnce();
  ok &= TestStreamKeyboardFocusTransitions();
  std::cout << "window_shift_state_test: " << (ok ? "PASS" : "FAIL") << '\n';
  return ok ? 0 : 1;
}
