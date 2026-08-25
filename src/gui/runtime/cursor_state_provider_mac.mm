#include "runtime/cursor_state_provider.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <CoreGraphics/CGRemoteOperation.h>

#include <cstdint>
#include <vector>

namespace crossdesk {
namespace {

struct CursorFingerprint {
  uint64_t pixel_hash = 0;
  size_t width = 0;
  size_t height = 0;
  size_t bytes_per_row = 0;
  CFIndex pixel_bytes = 0;
  NSPoint hot_spot = NSZeroPoint;
};

struct KnownCursor {
  CursorFingerprint fingerprint;
  CursorShape shape = CursorShape::default_cursor;
};

bool FingerprintCursor(NSCursor* cursor, CursorFingerprint* fingerprint) {
  if (!cursor || !fingerprint || !cursor.image) return false;

  NSRect proposed_rect = {NSZeroPoint, cursor.image.size};
  CGImageRef image =
      [cursor.image CGImageForProposedRect:&proposed_rect
                                   context:nil
                                     hints:nil];
  if (!image) return false;

  CGDataProviderRef provider = CGImageGetDataProvider(image);
  if (!provider) return false;

  CFDataRef data = CGDataProviderCopyData(provider);
  if (!data) return false;

  // NSCursor.currentSystemCursor returns a fresh NSCursor/NSImage wrapper on
  // each query. Hash the decoded CGImage pixels so it can still be matched to
  // AppKit's semantic cursor instances without encoding a large TIFF every
  // frame.
  constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
  constexpr uint64_t kFnvPrime = 1099511628211ULL;
  uint64_t hash = kFnvOffsetBasis;
  const UInt8* bytes = CFDataGetBytePtr(data);
  const CFIndex length = CFDataGetLength(data);
  for (CFIndex index = 0; index < length; ++index) {
    hash ^= bytes[index];
    hash *= kFnvPrime;
  }

  fingerprint->pixel_hash = hash;
  fingerprint->width = CGImageGetWidth(image);
  fingerprint->height = CGImageGetHeight(image);
  fingerprint->bytes_per_row = CGImageGetBytesPerRow(image);
  fingerprint->pixel_bytes = length;
  fingerprint->hot_spot = cursor.hotSpot;
  CFRelease(data);
  return true;
}

bool SameCursor(const CursorFingerprint& left,
                const CursorFingerprint& right) {
  return left.pixel_hash == right.pixel_hash && left.width == right.width &&
         left.height == right.height &&
         left.bytes_per_row == right.bytes_per_row &&
         left.pixel_bytes == right.pixel_bytes &&
         NSEqualPoints(left.hot_spot, right.hot_spot);
}

void AddKnownCursor(std::vector<KnownCursor>* cursors, NSCursor* cursor,
                    CursorShape shape) {
  CursorFingerprint fingerprint;
  if (FingerprintCursor(cursor, &fingerprint)) {
    cursors->push_back({fingerprint, shape});
  }
}

std::vector<KnownCursor> BuildKnownCursors() {
  std::vector<KnownCursor> cursors;
  cursors.reserve(16);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  AddKnownCursor(&cursors, NSCursor.arrowCursor, CursorShape::default_cursor);
  AddKnownCursor(&cursors, NSCursor.pointingHandCursor, CursorShape::pointer);
  AddKnownCursor(&cursors, NSCursor.crosshairCursor, CursorShape::crosshair);
  AddKnownCursor(&cursors, NSCursor.IBeamCursor, CursorShape::text);
  AddKnownCursor(&cursors, NSCursor.IBeamCursorForVerticalLayout,
                 CursorShape::text);
  AddKnownCursor(&cursors, NSCursor.operationNotAllowedCursor,
                 CursorShape::not_allowed);
  AddKnownCursor(&cursors, NSCursor.dragLinkCursor, CursorShape::alias);
  AddKnownCursor(&cursors, NSCursor.dragCopyCursor, CursorShape::copy);
  AddKnownCursor(&cursors, NSCursor.openHandCursor, CursorShape::grab);
  AddKnownCursor(&cursors, NSCursor.closedHandCursor, CursorShape::grabbing);
  AddKnownCursor(&cursors, NSCursor.resizeLeftRightCursor,
                 CursorShape::ew_resize);
  AddKnownCursor(&cursors, NSCursor.resizeUpDownCursor, CursorShape::ns_resize);
  AddKnownCursor(&cursors, NSCursor.resizeUpCursor, CursorShape::n_resize);
  AddKnownCursor(&cursors, NSCursor.resizeRightCursor, CursorShape::e_resize);
  AddKnownCursor(&cursors, NSCursor.resizeDownCursor, CursorShape::s_resize);
  AddKnownCursor(&cursors, NSCursor.resizeLeftCursor, CursorShape::w_resize);
#pragma clang diagnostic pop

  return cursors;
}

CursorShape ShapeFromMacCursor(NSCursor* cursor) {
  CursorFingerprint fingerprint;
  if (!FingerprintCursor(cursor, &fingerprint)) {
    return CursorShape::default_cursor;
  }

  // Sampling begins from the UI tick, after AppKit has initialized its cursor
  // catalog. Cache those fingerprints because only the current cursor needs
  // to be decoded on subsequent frames.
  static const std::vector<KnownCursor> known_cursors = BuildKnownCursors();
  for (const KnownCursor& known : known_cursors) {
    if (SameCursor(fingerprint, known.fingerprint)) return known.shape;
  }
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
  state->shape = state->visible ? ShapeFromMacCursor(cursor) : CursorShape::none;
  return true;
}

}  // namespace crossdesk

#endif
