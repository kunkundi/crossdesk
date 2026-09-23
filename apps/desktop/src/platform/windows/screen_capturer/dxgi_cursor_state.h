#ifndef CROSSDESK_DXGI_CURSOR_STATE_H_
#define CROSSDESK_DXGI_CURSOR_STATE_H_

#include <cstdint>
#include <mutex>

namespace crossdesk {

// DXGI's Visible flag describes the separate hardware pointer, not the
// system cursor. When it is false, a visible system cursor can already be
// included in the desktop texture (for example, during window dragging).
// Share that distinction with the GUI thread without changing cursor input
// coordinates or the wire protocol.
class DxgiCursorState {
 public:
  void BeginOutput(void* monitor, bool software_cursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    // usbmmidd can composite the cursor into the desktop without ever sending
    // a separate pointer update. Preserve this output-specific default across
    // privacy restoration; an explicit DXGI update can still override it.
    software_cursor_monitor_ = software_cursor ? monitor : nullptr;
    embedded_monitor_ = privacy_cursor_hidden_ ? nullptr : software_cursor_monitor_;
  }

  void Update(int64_t last_mouse_update_time, bool separate_pointer_visible,
              void* monitor) {
    // PointerPosition is undefined on desktop-only updates. Retain the last
    // pointer state until DXGI actually reports another pointer update.
    if (last_mouse_update_time == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (privacy_cursor_hidden_) return;
    embedded_monitor_ = separate_pointer_visible ? nullptr : monitor;
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    embedded_monitor_ = nullptr;
    software_cursor_monitor_ = nullptr;
  }

  void SetPrivacyCursorHidden(bool hidden) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (privacy_cursor_hidden_ == hidden) return;
    privacy_cursor_hidden_ = hidden;
    // Discard stale pointer-plane updates, then restore the output's default.
    // A software-cursor IDD may never send a fresh pointer-plane update.
    embedded_monitor_ = hidden ? nullptr : software_cursor_monitor_;
  }

  bool ShouldDrawCursor(bool system_cursor_visible, void* cursor_monitor) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // MagShowSystemCursor(FALSE) hides the local pointer plane without changing
    // CURSOR_SHOWING. DXGI Visible=false then means hidden, not embedded in the
    // captured frame. Keep sending the application's cursor to the controller.
    // CURSOR_SHOWING still takes precedence when the application hides it.
    if (privacy_cursor_hidden_) return system_cursor_visible;
    // A hidden cursor or a pointer on another monitor also makes DXGI report
    // Visible=false. Only suppress a visible cursor on the captured monitor.
    return system_cursor_visible &&
           (!cursor_monitor || embedded_monitor_ != cursor_monitor);
  }

 private:
  mutable std::mutex mutex_;
  void* embedded_monitor_ = nullptr;
  void* software_cursor_monitor_ = nullptr;
  bool privacy_cursor_hidden_ = false;
};

inline DxgiCursorState& SharedDxgiCursorState() {
  static DxgiCursorState state;
  return state;
}

}  // namespace crossdesk

#endif  // CROSSDESK_DXGI_CURSOR_STATE_H_
