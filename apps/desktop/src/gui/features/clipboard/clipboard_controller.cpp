#include "features/clipboard/clipboard_controller.h"

#include <cstring>
#include <shared_mutex>
#include <utility>

#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {
namespace {

// Keep a reliable clipboard message within MiniRTC/KCP's single-message
// fragmentation window (MTU is configured to 1200 bytes).
constexpr size_t kMaxClipboardTextBytes = 128 * 1024;

} // namespace

ClipboardController::ClipboardController(GuiRuntime &owner) : owner_(owner) {}

void ClipboardController::SetEventType(uint32_t event_type) {
  event_type_ = event_type;
}

uint32_t ClipboardController::event_type() const { return event_type_; }

void ClipboardController::Initialize() {
  last_text_.clear();

  char *clipboard_text = SDL_GetClipboardText();
  if (clipboard_text) {
    last_text_.assign(clipboard_text);
    SDL_free(clipboard_text);
  }

  events_enabled_.store(true, std::memory_order_release);
}

void ClipboardController::Shutdown() {
  events_enabled_.store(false, std::memory_order_release);
  std::lock_guard<std::mutex> lock(pending_mutex_);
  pending_remote_text_.reset();
}

void ClipboardController::QueueRemoteText(const char *data, size_t size) {
  if (!events_enabled_.load(std::memory_order_acquire)) {
    return;
  }
  if (!data || size == 0) {
    return;
  }
  if (size > kMaxClipboardTextBytes) {
    LOG_WARN("Ignore oversized remote clipboard text: {} bytes", size);
    return;
  }
  if (std::memchr(data, '\0', size) != nullptr) {
    LOG_WARN("Ignore remote clipboard text containing an embedded NUL byte");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (!events_enabled_.load(std::memory_order_relaxed)) {
      return;
    }
    // Only the newest clipboard value matters. Replacing it also bounds the
    // amount of memory a busy or malicious peer can queue.
    pending_remote_text_ = std::string(data, size);
  }

  SDL_Event event{};
  event.type = event_type_;
  if (event_type_ != 0 && !SDL_PushEvent(&event)) {
    // MainLoop also drains the pending value after its wait timeout.
    LOG_WARN("Failed to wake SDL loop for remote clipboard text: {}",
             SDL_GetError());
  }
}

void ClipboardController::ApplyPendingRemoteText() {
  if (!events_enabled_.load(std::memory_order_acquire)) {
    return;
  }

  std::optional<std::string> pending_text;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_text.swap(pending_remote_text_);
  }
  if (!pending_text || *pending_text == last_text_) {
    return;
  }

  // SDL clipboard functions must run on the thread that initialized SDL.
  if (!SDL_SetClipboardText(pending_text->c_str())) {
    LOG_ERROR("Failed to set remote clipboard text: {}", SDL_GetError());
    return;
  }

  // SDL will normally emit SDL_EVENT_CLIPBOARD_UPDATE for this write. Update
  // the baseline first so that event cannot echo the text back to the sender.
  last_text_ = std::move(*pending_text);
}

void ClipboardController::HandleLocalUpdate() {
  if (!events_enabled_.load(std::memory_order_acquire)) {
    return;
  }
  if (!SDL_HasClipboardText()) {
    last_text_.clear();
    return;
  }

  char *clipboard_text = SDL_GetClipboardText();
  if (!clipboard_text) {
    LOG_WARN("Failed to read local clipboard text: {}", SDL_GetError());
    return;
  }

  std::string text(clipboard_text);
  SDL_free(clipboard_text);
  if (text == last_text_) {
    return;
  }

  // Record the value before sending. Duplicate SDL notifications and a
  // remote write of the same value must not create a clipboard feedback loop.
  last_text_ = text;
  if (text.empty()) {
    return;
  }
  if (text.size() > kMaxClipboardTextBytes) {
    LOG_WARN("Ignore oversized local clipboard text: {} bytes", text.size());
    return;
  }

  SendToPeers(text);
}

int ClipboardController::SendToPeers(const std::string &text) {
  std::shared_lock lock(owner_.remote_sessions_mutex_);
  for (const auto &[remote_id, props] : owner_.remote_sessions_) {
    if (!props || !props->peer_ || !props->connection_established_ ||
        !props->enable_mouse_control_) {
      continue;
    }

    const int ret =
        SendReliableDataFrame(props->peer_, text.data(), text.size(),
                              props->clipboard_label_.c_str());
    if (ret != 0) {
      LOG_WARN("Failed to send clipboard data to peer [{}], ret={}",
               remote_id.c_str(), ret);
      return ret;
    }
  }

  if (owner_.peer_) {
    const int ret =
        SendReliableDataFrame(owner_.peer_, text.data(), text.size(),
                              owner_.clipboard_label_.c_str());
    if (ret != 0) {
      LOG_WARN("Failed to send clipboard data to peer [{}], ret={}",
               owner_.remote_id_display_, ret);
      return ret;
    }
  }

  return 0;
}

} // namespace crossdesk
