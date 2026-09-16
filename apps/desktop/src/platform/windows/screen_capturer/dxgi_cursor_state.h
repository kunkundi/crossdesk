#ifndef CROSSDESK_DXGI_CURSOR_STATE_H_
#define CROSSDESK_DXGI_CURSOR_STATE_H_

#include <atomic>
#include <cstdint>

namespace crossdesk {

// DXGI's Visible flag describes the separate hardware pointer, not the
// system cursor. When it is false, a visible system cursor can already be
// included in the desktop texture (for example, during window dragging).
// Share that distinction with the GUI thread without changing cursor input
// coordinates or the wire protocol.
class DxgiCursorState {
 public:
  void Update(int64_t last_mouse_update_time, bool separate_pointer_visible,
              void* monitor) {
    // PointerPosition is undefined on desktop-only updates. Retain the last
    // pointer state until DXGI actually reports another pointer update.
    if (last_mouse_update_time == 0) return;
    embedded_monitor_.store(separate_pointer_visible ? nullptr : monitor,
                            std::memory_order_relaxed);
  }

  void Reset() {
    embedded_monitor_.store(nullptr, std::memory_order_relaxed);
  }

  bool ShouldDrawCursor(bool system_cursor_visible, void* cursor_monitor) const {
    // A hidden cursor or a pointer on another monitor also makes DXGI report
    // Visible=false. Only suppress a visible cursor on the captured monitor.
    return system_cursor_visible &&
           (!cursor_monitor ||
            embedded_monitor_.load(std::memory_order_relaxed) != cursor_monitor);
  }

 private:
  std::atomic<void*> embedded_monitor_{nullptr};
};

inline DxgiCursorState& SharedDxgiCursorState() {
  static DxgiCursorState state;
  return state;
}

}  // namespace crossdesk

#endif  // CROSSDESK_DXGI_CURSOR_STATE_H_
