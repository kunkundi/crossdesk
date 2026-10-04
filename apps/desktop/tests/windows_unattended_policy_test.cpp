#include <cstdlib>
#include <iostream>

#include "platform/windows/service/unattended_policy.h"

int main() {
  using crossdesk::UnattendedAction;
  using Action = crossdesk::UnattendedProcessAction;
  const auto expect = [](bool value, const char* message) {
    if (!value) {
      std::cerr << message << '\n';
      std::exit(1);
    }
  };
  constexpr uint32_t none = 0xFFFFFFFFu;
  expect(UnattendedAction(false, 1, false, none) == Action::keep,
         "fresh installs must not start a network host");
  expect(UnattendedAction(true, none, false, none) == Action::keep,
         "boot before a console exists must wait");
  expect(UnattendedAction(true, 0, false, none) == Action::keep,
         "never capture service session zero");
  expect(UnattendedAction(true, 1, false, none) == Action::start,
         "a pre-login console must start the host");
  expect(UnattendedAction(true, 1, true, 1) == Action::keep,
         "login, lock and GUI exit must retain the same host");
  expect(UnattendedAction(true, 2, true, 1) == Action::stop,
         "user switching must stop capture in the old console first");
  expect(UnattendedAction(true, 2, false, none) == Action::start,
         "a replacement console must start after the old host exits");
  expect(UnattendedAction(true, none, true, 2) == Action::stop,
         "a detached console must not keep sending frames");
  expect(UnattendedAction(false, 2, true, 2) == Action::stop,
         "disabling unattended mode must stop its host");
  expect(UnattendedAction(true, 2, false, 2) == Action::start,
         "a crashed host must restart even if its session is unchanged");
  std::cout << "Unattended process lifecycle checks passed\n";
}
