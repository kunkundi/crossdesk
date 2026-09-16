#ifndef CROSSDESK_SECURE_DESKTOP_CURSOR_STATE_H_
#define CROSSDESK_SECURE_DESKTOP_CURSOR_STATE_H_

#include <chrono>
#include <cstdint>
#include <mutex>

namespace crossdesk {

// Fixed-width IPC metadata. Cursor handles belong to the helper's desktop and
// must be converted to a semantic shape before crossing the process boundary.
struct SecureDesktopCursorSnapshot {
  uint32_t valid = 0;
  uint32_t visible = 0;
  uint32_t shape = 0;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t render_mode = 0;
  uint32_t hidden_reason = 0;
};
static_assert(sizeof(SecureDesktopCursorSnapshot) == 28);

// The capture thread publishes helper samples for the GUI thread. While secure
// capture is active, even an unavailable sample must not fall back to sampling
// the ordinary desktop, which can report a hidden or stale cursor.
class SecureDesktopCursorState {
 public:
  void SetActive(bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ != active) snapshot_ = {};
    active_ = active;
  }

  void Update(const SecureDesktopCursorSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
      snapshot_ = snapshot;
      updated_at_ = std::chrono::steady_clock::now();
    }
  }

  bool Get(SecureDesktopCursorSnapshot* snapshot) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !snapshot) return false;
    *snapshot = snapshot_;
    if (std::chrono::steady_clock::now() - updated_at_ >=
        std::chrono::milliseconds(1500)) {
      snapshot->valid = 0;
    }
    return true;
  }

 private:
  mutable std::mutex mutex_;
  bool active_ = false;
  SecureDesktopCursorSnapshot snapshot_{};
  std::chrono::steady_clock::time_point updated_at_{};
};

inline SecureDesktopCursorState& SharedSecureDesktopCursorState() {
  static SecureDesktopCursorState state;
  return state;
}

}  // namespace crossdesk

#endif  // CROSSDESK_SECURE_DESKTOP_CURSOR_STATE_H_
