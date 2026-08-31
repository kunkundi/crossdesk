#ifndef CROSSDESK_COMMON_LINUX_CURSOR_SHAPE_H_
#define CROSSDESK_COMMON_LINUX_CURSOR_SHAPE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <remote_cursor_shape.h>

namespace crossdesk {

struct LinuxCursorAlias {
  const char* name;
  RemoteCursorShape shape;
};

const LinuxCursorAlias* LinuxCursorAliases(size_t* count);
RemoteCursorShape ShapeFromLinuxCursorName(const std::string& name);

struct LinuxCursorImageView {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t xhot = 0;
  uint32_t yhot = 0;
  const uint32_t* argb = nullptr;
};

// Matches a compositor-provided cursor bitmap against the active Xcursor
// theme. This is needed on Wayland, where XFixes only observes XWayland
// cursors and cannot report cursors owned by native Wayland surfaces.
class LinuxCursorThemeMatcher {
 public:
  LinuxCursorThemeMatcher();
  ~LinuxCursorThemeMatcher();

  LinuxCursorThemeMatcher(const LinuxCursorThemeMatcher&) = delete;
  LinuxCursorThemeMatcher& operator=(const LinuxCursorThemeMatcher&) = delete;

  RemoteCursorShape Match(const LinuxCursorImageView& image);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crossdesk

#endif  // CROSSDESK_COMMON_LINUX_CURSOR_SHAPE_H_
