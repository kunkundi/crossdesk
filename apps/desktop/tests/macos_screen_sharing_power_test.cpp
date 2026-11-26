#include <IOKit/pwr_mgt/IOPMLib.h>

#include <iostream>
#include <vector>

namespace {

std::vector<IOPMAssertionID> created;
std::vector<IOPMAssertionID> released;
int wake_requests = 0;
std::size_t inhibitors_at_wake = 0;
bool wake_succeeds = true;

IOReturn CreateSleepInhibitor(CFStringRef, IOPMAssertionLevel, CFStringRef,
                              IOPMAssertionID* id) {
  *id = static_cast<IOPMAssertionID>(created.size() + 1);
  created.push_back(*id);
  return kIOReturnSuccess;
}

IOReturn WakeDisplay(CFStringRef, IOPMUserActiveType, IOPMAssertionID* id) {
  ++wake_requests;
  inhibitors_at_wake = created.size();
  if (!wake_succeeds) return kIOReturnNotPermitted;
  *id = 100;
  return kIOReturnSuccess;
}

IOReturn ReleaseAssertion(IOPMAssertionID id) {
  released.push_back(id);
  return kIOReturnSuccess;
}

}  // namespace

// Exercise the real sharing lifecycle without changing this Mac's power state.
#define _RD_LOG_H_
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define IOPMAssertionCreateWithName CreateSleepInhibitor
#define IOPMAssertionDeclareUserActivity WakeDisplay
#define IOPMAssertionRelease ReleaseAssertion
#include "../src/gui/features/devices/screen_sharing_power.cpp"

int main() {
  crossdesk::ScreenSharingPower power;
  power.SetSharing(false);
  if (!created.empty() || wake_requests != 0) return 1;

  power.SetSharing(true);
  power.SetSharing(true);
  if (created.size() != 2 || wake_requests != 1 || inhibitors_at_wake != 2) {
    std::cerr << "Expected one wake request after acquiring both sleep inhibitors\n";
    return 1;
  }

  power.SetSharing(false);
  power.SetSharing(false);
  if (released != std::vector<IOPMAssertionID>{100, 2, 1}) return 1;

  wake_succeeds = false;
  power.SetSharing(true);
  power.SetSharing(false);
  if (wake_requests != 2 ||
      released != std::vector<IOPMAssertionID>{100, 2, 1, 4, 3}) {
    return 1;
  }
  std::cout << "macOS screen sharing power: PASS\n";
}
