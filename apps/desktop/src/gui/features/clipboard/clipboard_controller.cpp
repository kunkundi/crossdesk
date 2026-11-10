#include "features/clipboard/clipboard_controller.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>

#include <cstring>
#include <shared_mutex>
#include <utility>

#include "rd_log.h"
#include "runtime/gui_runtime.h"
#include "web_clipboard_protocol.h"

namespace crossdesk {
namespace {

// Keep a reliable clipboard message within MiniRTC/KCP's single-message
// fragmentation window (MTU is configured to 1200 bytes).
constexpr size_t kMaxClipboardTextBytes = 128 * 1024;

} // namespace

struct ClipboardController::WebSession {
  std::weak_ptr<PeerEventHandler> events;
  web_clipboard::Assembler assembler;
  std::atomic<bool> ready{false};
};

ClipboardController::ClipboardController(GuiRuntime &owner) : owner_(owner) {}

void ClipboardController::SetEventType(uint32_t event_type) {
  event_type_ = event_type;
}

uint32_t ClipboardController::event_type() const { return event_type_; }

void ClipboardController::Initialize() {
  last_text_.clear();
  deferred_local_session_.reset();

  char *clipboard_text = SDL_GetClipboardText();
  if (clipboard_text) {
    last_text_.assign(clipboard_text);
    SDL_free(clipboard_text);
  }

  events_enabled_.store(true, std::memory_order_release);
}

void ClipboardController::Shutdown() {
  events_enabled_.store(false, std::memory_order_release);
  deferred_local_session_.reset();
  {
    std::lock_guard lock(web_sessions_mutex_);
    web_sessions_.clear();
  }
  std::lock_guard<std::mutex> lock(pending_mutex_);
  pending_remote_text_.reset();
  pending_complete_ = {};
  pending_allowed_ = {};
  pending_reads_.clear();
}

bool ClipboardController::WebSessionActive(
    const std::string& remote_id, const std::shared_ptr<WebSession>& session) {
  if (!session || !events_enabled_.load(std::memory_order_acquire) ||
      !owner_.IsAuthorizedController(remote_id)) return false;
  const auto events = session->events.lock();
  if (!events || events != owner_.ControllerEvents(remote_id) ||
      !events->DataAllowed()) return false;
  std::lock_guard lock(web_sessions_mutex_);
  const auto found = web_sessions_.find(remote_id);
  return found != web_sessions_.end() && found->second == session;
}

void ClipboardController::ForgetWeb(const std::string& remote_id) {
  std::lock_guard lock(web_sessions_mutex_);
  web_sessions_.erase(remote_id);
}

void ClipboardController::ReceiveWeb(const char* data, size_t size,
                                      const std::string& remote_id,
                                      PeerEventHandler* source) {
  namespace clip = web_clipboard;
  if (!events_enabled_.load(std::memory_order_acquire) || owner_.unattended_host_ ||
      !owner_.IsAuthorizedController(remote_id) || !data ||
      size < clip::kHeaderBytes || size > clip::kHeaderBytes + clip::kChunkBytes)
    return;
  const auto events = owner_.ControllerEvents(remote_id);
  if (!events || events.get() != source || !events->DataAllowed()) return;
  const auto kind = clip::Read32(data), request = clip::Read32(data + 4);
  const bool hello = kind == clip::kHello && !request && size == clip::kHeaderBytes &&
                     clip::Read32(data + 8) == clip::kVersion && !clip::Read32(data + 12);
  std::shared_ptr<WebSession> session;
  std::string message;
  {
    std::lock_guard lock(web_sessions_mutex_);
    if (!events_enabled_.load(std::memory_order_relaxed)) return;
    auto found = web_sessions_.find(remote_id);
    if (hello) {
      // A new application handshake also invalidates unfinished writes from
      // an earlier channel on the same controller connection.
      session = std::make_shared<WebSession>();
      session->events = events;
      web_sessions_[remote_id] = session;
    } else {
      if (found == web_sessions_.end() || found->second->events.lock() != events) return;
      session = found->second;
    }
    if (!hello && kind != clip::kRead && kind != clip::kClear) {
      if (!session->ready.load(std::memory_order_acquire) ||
          kind != clip::kText || !request) return;
      message = session->assembler.Receive(std::string_view(data, size));
    }
  }
  if (hello) {
    if (!WebSessionActive(remote_id, session)) return;
    const auto ready = clip::Frame(clip::kReady, 0, clip::kVersion);
    if (owner_.SendControllerData(remote_id, ready.data(), ready.size(),
                                  clip::kStream) == 0)
      session->ready.store(true, std::memory_order_release);
    return;
  }
  if ((kind == clip::kRead || kind == clip::kClear) &&
      (!session->ready.load(std::memory_order_acquire) || !request ||
       size != clip::kHeaderBytes)) return;
  if (kind == clip::kRead) {
    const auto send = [this, remote_id, session](const std::string& frame) {
      return WebSessionActive(remote_id, session) &&
          owner_.SendControllerData(remote_id, frame.data(), frame.size(), clip::kStream) == 0;
    };
    if (!QueueRead([send, request](std::optional<std::string> text) {
      if (!text) send(clip::Frame(clip::kRejected, request));
      else if (text->empty()) send(clip::Frame(clip::kEmpty, request));
      else if (!clip::SendText(*text, request, 16 * 1024, send))
        send(clip::Frame(clip::kRejected, request));
    })) send(clip::Frame(clip::kRejected, request));
    return;
  }
  if (kind == clip::kClear) message = clip::Frame(clip::kText, request);
  if (message.empty()) return;
  const std::weak_ptr<WebSession> weak = session;
  auto allowed = [this, remote_id, weak] {
    return WebSessionActive(remote_id, weak.lock());
  };
  auto complete = [this, remote_id, request, allowed](bool applied) {
    if (!allowed()) return;
    const auto ack = clip::Frame(applied ? clip::kApplied : clip::kRejected, request);
    owner_.SendControllerData(remote_id, ack.data(), ack.size(), clip::kStream);
  };
  QueueRemoteText(message.data() + clip::kHeaderBytes,
                 message.size() - clip::kHeaderBytes, {},
                 std::move(complete), std::move(allowed));
}

int ClipboardController::SendToController(const std::string& remote_id,
                                          const std::string& text) {
  std::shared_ptr<WebSession> session;
  {
    std::lock_guard lock(web_sessions_mutex_);
    const auto found = web_sessions_.find(remote_id);
    if (found != web_sessions_.end()) session = found->second;
  }
  if (!session) return owner_.SendControllerData(
      remote_id, text.data(), text.size(), owner_.clipboard_label_.c_str());
  if (!session->ready.load(std::memory_order_acquire)) return 0;
  // Framing belongs to CrossDesk. Keep each complete packet within 16 KiB.
  return web_clipboard::SendText(text, 0, 16 * 1024,
      [this, &remote_id, &session](const std::string& frame) {
        return WebSessionActive(remote_id, session) &&
               owner_.SendControllerData(remote_id, frame.data(), frame.size(),
                                        web_clipboard::kStream) == 0;
      }) ? 0 : -1;
}

void ClipboardController::QueueRemoteText(const char* data, size_t size,
                                          const std::string& remote_id,
                                          std::function<void(bool)> complete,
                                          std::function<bool()> allowed) {
  if (!events_enabled_.load(std::memory_order_acquire)) {
    return;
  }
  if (!data || (size == 0 && !complete)) {
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

  const auto session =
      remote_id.empty() ? nullptr : owner_.FindRemoteSession(remote_id);
  if (!remote_id.empty() && (!session || !session->clipboard_allowed_.load()))
    return;
  const auto generation = session ? session->input_generation_.load() : 0;
  std::function<void(bool)> superseded;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (!events_enabled_.load(std::memory_order_relaxed)) {
      return;
    }
    // Only the newest clipboard value matters. Replacing it also bounds the
    // amount of memory a busy or malicious peer can queue.
    pending_remote_text_ = std::string(data, size);
    pending_remote_id_ = remote_id;
    pending_session_ = session;
    pending_generation_ = generation;
    superseded = std::move(pending_complete_);
    pending_complete_ = std::move(complete);
    pending_allowed_ = std::move(allowed);
  }
  if (superseded) superseded(false);

  SDL_Event event{};
  event.type = event_type_;
  if (event_type_ != 0 && !SDL_PushEvent(&event)) {
    // MainLoop also drains the pending value after its wait timeout.
    LOG_WARN("Failed to wake SDL loop for remote clipboard text: {}",
             SDL_GetError());
  }
}

bool ClipboardController::QueueRead(std::function<void(std::optional<std::string>)> complete) {
  {
    std::lock_guard lock(pending_mutex_);
    if (!events_enabled_.load() || pending_reads_.size() >= 32) return false;
    pending_reads_.push_back(std::move(complete));
  }
  SDL_Event event{};
  event.type = event_type_;
  if (event_type_) SDL_PushEvent(&event);
  return true;
}

void ClipboardController::ApplyPendingRemoteText() {
  if (!events_enabled_.load(std::memory_order_acquire)) {
    return;
  }
  ApplyPendingWrite();
  std::deque<std::function<void(std::optional<std::string>)>> reads;
  {
    std::lock_guard lock(pending_mutex_);
    reads.swap(pending_reads_);
  }
  if (!reads.empty()) {
    char* value = SDL_GetClipboardText();
    const std::optional<std::string> text = value ? std::optional<std::string>{value} : std::nullopt;
    SDL_free(value);
    for (auto& complete : reads) complete(text);
  }
}

void ClipboardController::ApplyPendingWrite() {
  std::optional<std::string> pending_text;
  std::string source_id;
  std::weak_ptr<gui_detail::RemoteSession> source_session;
  uint64_t generation = 0;
  std::function<void(bool)> complete;
  std::function<bool()> allowed;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_text.swap(pending_remote_text_);
    source_id.swap(pending_remote_id_);
    source_session.swap(pending_session_);
    generation = pending_generation_;
    complete.swap(pending_complete_);
    allowed.swap(pending_allowed_);
  }
  if (!source_id.empty()) {
    auto props = owner_.FindRemoteSession(source_id);
    if (!props || props != source_session.lock() ||
        !props->clipboard_allowed_.load() ||
        props->input_generation_.load() != generation) {
      if (complete) complete(false);
      return;
    }
  }
  if (!pending_text || (allowed && !allowed())) {
    if (complete) complete(false);
    return;
  }

  // SDL clipboard functions must run on the thread that initialized SDL.
  // Always apply remote writes: the local OS clipboard may have changed since
  // the last poll, even when the received text still matches last_text_.
  if (!SDL_SetClipboardText(pending_text->c_str())) {
    LOG_ERROR("Failed to set remote clipboard text: {}", SDL_GetError());
    if (complete) complete(false);
    return;
  }

  // SDL will normally emit SDL_EVENT_CLIPBOARD_UPDATE for this write. Update
  // the baseline first so that event cannot echo the text back to the sender.
  last_text_ = std::move(*pending_text);
  deferred_local_session_.reset();
  if (complete) complete(true);
}

void ClipboardController::HandleLocalUpdate() {
  if (!events_enabled_.load(std::memory_order_acquire)) {
    return;
  }
  if (!SDL_HasClipboardText()) {
    last_text_.clear();
    deferred_local_session_.reset();
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
    SendDeferredLocalText();
    return;
  }

  // Record the value before sending. Duplicate SDL notifications and a
  // remote write of the same value must not create a clipboard feedback loop.
  last_text_ = text;
  deferred_local_session_.reset();
  if (text.empty()) {
    return;
  }
  if (text.size() > kMaxClipboardTextBytes) {
    LOG_WARN("Ignore oversized local clipboard text: {} bytes", text.size());
    return;
  }

  SendToPeers(text);
}

void ClipboardController::SendDeferredLocalText() {
  const auto session = deferred_local_session_.lock();
  if (!session) {
    return;
  }

  std::shared_lock lock(owner_.remote_sessions_mutex_);
  const auto current = owner_.remote_sessions_.find(session->remote_id_);
  if (current == owner_.remote_sessions_.end() || current->second != session ||
      session->closing_ || !session->peer_ || !session->connection_established_ ||
      !session->tab_opened_ || !session->tab_selected_ ||
      !session->control_mouse_) {
    deferred_local_session_.reset();
    return;
  }
  if (!session->clipboard_allowed_.load()) {
    return;
  }

  const int ret = MiniRtcSendReliableDataFrame(
      session->peer_, last_text_.data(), last_text_.size(),
      session->clipboard_label_.c_str());
  if (ret != 0) {
    LOG_WARN("Failed to send deferred clipboard data, ret={}", ret);
    return;
  }
  deferred_local_session_.reset();
}

int ClipboardController::SendToPeers(const std::string &text) {
  std::shared_lock lock(owner_.remote_sessions_mutex_);
  for (const auto &[remote_id, props] : owner_.remote_sessions_) {
    if (!props || props->closing_ || !props->peer_ ||
        !props->connection_established_) {
      continue;
    }
    if (!props->clipboard_allowed_.load()) {
      // Copying in another local app temporarily releases keyboard control.
      // Keep this copy for the same selected stream, without sharing it with
      // background tabs or DeskHub previews.
      if (props->tab_opened_ && props->tab_selected_ && props->control_mouse_) {
        deferred_local_session_ = props;
      }
      continue;
    }

    const int ret =
        MiniRtcSendReliableDataFrame(props->peer_, text.data(), text.size(),
                              props->clipboard_label_.c_str());
    if (ret != 0) {
      LOG_WARN("Failed to send clipboard data, ret={}", ret);
      return ret;
    }
  }

  lock.unlock();
  for (const auto& id : owner_.ConnectedControllers()) {
    const int ret = SendToController(id, text);
    if (ret != 0) return ret;
  }

  return 0;
}

} // namespace crossdesk
