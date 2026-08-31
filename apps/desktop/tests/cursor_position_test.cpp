#include <iostream>
#include <vector>

#include <remote_action.h>

#include "cursor_position.h"
#include "display_info.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

}  // namespace

int main() {
  const std::vector<crossdesk::DisplayInfo> displays = {
      crossdesk::DisplayInfo("Test Display", 0, 0, 1920, 1080)};
  crossdesk::CursorState normalized_cursor{};
  const bool ok = Expect(
      crossdesk::NormalizeCursorPosition(960.0, 540.0, displays, 0,
                                         &normalized_cursor) &&
          normalized_cursor.position_valid &&
          normalized_cursor.x == 0.5f && normalized_cursor.y == 0.5f,
      "cursor position should use continuous display extents");
  return ok ? 0 : 1;
}
