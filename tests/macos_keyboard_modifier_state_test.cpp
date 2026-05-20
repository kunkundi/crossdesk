#include "macos_keyboard_modifier_state.h"

#include <cstdint>
#include <iostream>

namespace {

bool ExpectEqual(const char* name, uint32_t actual, uint32_t expected) {
  if (actual == expected) {
    return true;
  }

  std::cerr << name << " mismatch\n"
            << "  expected: " << expected << "\n"
            << "  actual:   " << actual << "\n";
  return false;
}

}  // namespace

int main() {
  crossdesk::MacKeyboardModifierState state;

  bool ok = true;
  ok &= ExpectEqual("initial flags", state.flags(), 0);
  ok &= ExpectEqual("left shift down", state.Update(0xA0, true),
                    crossdesk::kMacInjectedModifierShift);
  ok &= ExpectEqual("shifted semicolon keeps shift",
                    state.Update(0xBA, true),
                    crossdesk::kMacInjectedModifierShift);
  ok &= ExpectEqual("semicolon up keeps shift", state.Update(0xBA, false),
                    crossdesk::kMacInjectedModifierShift);
  ok &= ExpectEqual("right shift down while left held",
                    state.Update(0xA1, true),
                    crossdesk::kMacInjectedModifierShift);
  ok &= ExpectEqual("left shift up while right held", state.Update(0xA0, false),
                    crossdesk::kMacInjectedModifierShift);
  ok &= ExpectEqual("right shift up clears shift", state.Update(0xA1, false),
                    0);

  ok &= ExpectEqual("left control down", state.Update(0xA2, true),
                    crossdesk::kMacInjectedModifierControl);
  ok &= ExpectEqual("right alt adds option", state.Update(0xA5, true),
                    crossdesk::kMacInjectedModifierControl |
                        crossdesk::kMacInjectedModifierOption);
  ok &= ExpectEqual("left command adds command", state.Update(0x5B, true),
                    crossdesk::kMacInjectedModifierControl |
                        crossdesk::kMacInjectedModifierOption |
                        crossdesk::kMacInjectedModifierCommand);
  ok &= ExpectEqual("left control up leaves option command",
                    state.Update(0xA2, false),
                    crossdesk::kMacInjectedModifierOption |
                        crossdesk::kMacInjectedModifierCommand);
  ok &= ExpectEqual("right alt up leaves command", state.Update(0xA5, false),
                    crossdesk::kMacInjectedModifierCommand);
  ok &= ExpectEqual("left command up clears all", state.Update(0x5B, false),
                    0);

  return ok ? 0 : 1;
}
