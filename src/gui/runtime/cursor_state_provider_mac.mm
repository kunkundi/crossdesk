#include "runtime/cursor_state_provider.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <CoreGraphics/CGRemoteOperation.h>

namespace crossdesk {
namespace {

bool SameCursor(NSCursor* left, NSCursor* right) {
  if (left == right || [left isEqual:right]) return true;
  return left && right && NSEqualPoints(left.hotSpot, right.hotSpot) &&
         [left.image isEqual:right.image];
}

CursorShape ShapeFromMacCursor(NSCursor* cursor) {
  if (SameCursor(cursor, NSCursor.pointingHandCursor))
    return CursorShape::pointer;
  if (SameCursor(cursor, NSCursor.crosshairCursor))
    return CursorShape::crosshair;
  if (SameCursor(cursor, NSCursor.IBeamCursor) ||
      SameCursor(cursor, NSCursor.IBeamCursorForVerticalLayout))
    return CursorShape::text;
  if (SameCursor(cursor, NSCursor.operationNotAllowedCursor))
    return CursorShape::not_allowed;
  if (SameCursor(cursor, NSCursor.dragLinkCursor)) return CursorShape::alias;
  if (SameCursor(cursor, NSCursor.dragCopyCursor)) return CursorShape::copy;
  if (SameCursor(cursor, NSCursor.openHandCursor)) return CursorShape::grab;
  if (SameCursor(cursor, NSCursor.closedHandCursor))
    return CursorShape::grabbing;
  if (SameCursor(cursor, NSCursor.resizeLeftRightCursor))
    return CursorShape::ew_resize;
  if (SameCursor(cursor, NSCursor.resizeUpDownCursor))
    return CursorShape::ns_resize;
  if (SameCursor(cursor, NSCursor.resizeUpCursor))
    return CursorShape::n_resize;
  if (SameCursor(cursor, NSCursor.resizeRightCursor))
    return CursorShape::e_resize;
  if (SameCursor(cursor, NSCursor.resizeDownCursor))
    return CursorShape::s_resize;
  if (SameCursor(cursor, NSCursor.resizeLeftCursor))
    return CursorShape::w_resize;
  return CursorShape::default_cursor;
}

}  // namespace

struct CursorStateProvider::Impl {};

CursorStateProvider::CursorStateProvider() : impl_(std::make_unique<Impl>()) {}
CursorStateProvider::~CursorStateProvider() = default;

bool CursorStateProvider::Sample(CursorState* state) {
  if (!state) return false;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSCursor* cursor = NSCursor.currentSystemCursor;
  const bool visible = CGCursorIsVisible();
#pragma clang diagnostic pop

  state->seq = 0;
  state->visible = visible && cursor != nil;
  state->shape = state->visible ? ShapeFromMacCursor(cursor)
                                : CursorShape::none;
  return true;
}

}  // namespace crossdesk

#endif
