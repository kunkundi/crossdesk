#include "clipboard_controller.h"

#include <SDL3/SDL_clipboard.h>
#include <shared_mutex>

#include "clipboard.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

struct ClipboardController::FileSession {
  std::weak_ptr<PeerEventHandler> events;
  std::weak_ptr<gui_detail::RemoteSession> viewer;
  bool controlling = false;
  std::unique_ptr<ClipboardFileTransfer> transfer;
};

bool ClipboardController::FileSessionActive(const std::string& remote,
                                            const std::shared_ptr<FileSession>& session) {
  if (!session || !events_enabled_.load() || owner_.unattended_host_) return false;
  const auto events = session->events.lock();
  if (!events || !events->DataAllowed()) return false;
  if (session->controlling) {
    const auto viewer = session->viewer.lock();
    if (!viewer || viewer->closing_ || owner_.FindRemoteSession(remote) != viewer ||
        viewer->peer_events_ != events) return false;
  } else if (!owner_.IsAuthorizedController(remote) || owner_.ControllerEvents(remote) != events) {
    return false;
  }
  std::lock_guard lock(file_sessions_mutex_);
  const auto found = file_sessions_.find(remote);
  return found != file_sessions_.end() && found->second == session;
}

void ClipboardController::SetFileSupport(const std::string& remote, PeerEventHandler* source,
                                         bool supported) {
#ifndef __OHOS__
  if (!events_enabled_.load() || owner_.unattended_host_) return;
  const auto viewer = owner_.FindRemoteSession(remote);
  const auto events = viewer ? viewer->peer_events_ : owner_.ControllerEvents(remote);
  if (!events || events.get() != source || !events->DataAllowed()) return;
  std::shared_ptr<FileSession> previous;
  {
    std::lock_guard lock(file_sessions_mutex_);
    if (!events_enabled_.load()) return;
    auto& slot = file_sessions_[remote];
    if (supported && slot && slot->events.lock() == events) return;
    previous = std::move(slot);
    if (!supported) file_sessions_.erase(remote);
    else {
      auto session = std::make_shared<FileSession>();
      session->events = events; session->viewer = viewer; session->controlling = bool(viewer);
      const std::weak_ptr<FileSession> weak = session;
      session->transfer = std::make_unique<ClipboardFileTransfer>(
          [this, remote, weak](const std::string& packet) {
            const auto session = weak.lock();
            if (!FileSessionActive(remote, session)) return false;
            const auto events = session->events.lock();
            if (!events) return false;
            auto callback = events->EnterCallback();
            if (!callback || !events->DataAllowed()) return false;
            if (!session->controlling)
              return owner_.SendControllerData(remote, packet.data(), packet.size(),
                                                kClipboardFilesStream) == 0;
            const auto viewer = session->viewer.lock();
            std::shared_lock lock(owner_.remote_sessions_mutex_);
            return viewer && !viewer->closing_ && viewer->peer_ &&
                MiniRtcSendReliableDataFrame(viewer->peer_, packet.data(), packet.size(),
                                            kClipboardFilesStream) == 0;
          }, [this, remote, weak]() -> std::optional<uint64_t> {
            const auto session = weak.lock();
            if (!FileSessionActive(remote, session)) return {};
            if (session->controlling) {
              const auto viewer = session->viewer.lock();
              if (!viewer || !viewer->clipboard_allowed_.load()) return {};
            }
            std::lock_guard lock(pending_mutex_);
            return clipboard_revision_.load();
          }, [this, remote, weak](std::shared_ptr<ClipboardFilePromise> offer,
                                  uint64_t revision, uint32_t token) {
            if (!FileSessionActive(remote, weak.lock())) { offer->Cancel(); return; }
            const std::weak_ptr<ClipboardFilePromise> pending_offer = offer;
            offer->SetRequest([weak, pending_offer, token] {
              const auto session = weak.lock();
              if (session && session->transfer->Current(token)) session->transfer->Paste(token);
              else if (const auto offer = pending_offer.lock()) offer->Cancel();
            });
            std::optional<PendingFiles> previous;
            {
              std::lock_guard lock(pending_mutex_);
              previous.swap(pending_files_);
              if (events_enabled_.load())
                pending_files_ = PendingFiles{remote, weak, std::move(offer), revision, token};
            }
            if (previous) if (const auto old = previous->session.lock())
              old->transfer->CancelReceive(previous->token);
          }, ClipboardFileTransfer::Progress{},
          [this, weak, token = uint32_t{}, id = uint64_t{}]
              (const ClipboardFileTransfer::ReceiveProgress& progress) mutable {
            if (token != progress.token) {
              token = progress.token; id = ++next_file_progress_id_;
            }
            std::lock_guard lock(file_progress_mutex_);
            if (id <= dismissed_file_progress_id_ || (file_progress_ && file_progress_->id > id)) return;
            const auto now = std::chrono::steady_clock::now();
            if (!file_progress_ || file_progress_->id != id)
              file_progress_ = FileReceiveProgress{id, progress, now, now};
            else {
              if (file_progress_->transfer.status != progress.status) file_progress_->updated = now;
              file_progress_->transfer = progress;
            }
            file_progress_session_ = weak;
          });
      slot = std::move(session);
    }
  }
  if (previous) previous->transfer->Stop();
#endif
}

void ClipboardController::ReceiveFiles(const char* data, size_t size, const std::string& remote,
                                       PeerEventHandler* source) {
  std::shared_ptr<FileSession> session;
  {
    std::lock_guard lock(file_sessions_mutex_);
    const auto found = file_sessions_.find(remote);
    if (found != file_sessions_.end()) session = found->second;
  }
  if (FileSessionActive(remote, session) && session->events.lock().get() == source)
    session->transfer->Receive(data, size);
}

void ClipboardController::SendLocalFiles(bool changed) {
  std::vector<std::pair<std::string, std::shared_ptr<FileSession>>> sessions;
  {
    std::lock_guard lock(file_sessions_mutex_);
    for (const auto& entry : file_sessions_) sessions.push_back(entry);
  }
  for (const auto& [remote, session] : sessions) {
    if (!FileSessionActive(remote, session)) {
      {
        std::lock_guard lock(file_sessions_mutex_);
        const auto found = file_sessions_.find(remote);
        if (found != file_sessions_.end() && found->second == session) file_sessions_.erase(found);
      }
      session->transfer->Stop();
      continue;
    }
    if (changed) session->transfer->Copy({});
    if (!local_files_ || last_files_.empty()) continue;
    if (session->controlling) {
      const auto viewer = session->viewer.lock();
      if (!viewer) continue;
      if (!viewer->stream_window_open_ || !viewer->last_focused_stream_ || !viewer->control_mouse_) {
        if (deferred_file_session_.lock() == viewer) deferred_file_session_.reset();
        continue;
      }
      if (!viewer->clipboard_allowed_.load()) {
        if (changed && viewer->stream_window_open_ && viewer->last_focused_stream_ && viewer->control_mouse_)
          deferred_file_session_ = viewer;
        continue;
      }
      if (!changed && deferred_file_session_.lock() != viewer) continue;
      session->transfer->Copy(last_files_);
      deferred_file_session_.reset();
    } else if (changed) session->transfer->Copy(last_files_);
  }
}

void ClipboardController::ApplyPendingFiles() {
#ifndef __OHOS__
  if (file_offer_ && (!file_offer_->offer->valid() ||
      !FileSessionActive(file_offer_->remote, file_offer_->session.lock()))) ClearFileOffer();
  std::optional<PendingFiles> pending;
  { std::lock_guard lock(pending_mutex_); pending.swap(pending_files_); }
  if (!pending) return;
  const auto session = pending->session.lock();
  const auto reject = [&] { if (session) session->transfer->CancelReceive(pending->token); };
  // Observe a local copy even when SDL has not delivered its notification yet.
  HandleLocalUpdate();
  if (!FileSessionActive(pending->remote, session)) { reject(); return; }
  if (session->controlling) {
    const auto viewer = session->viewer.lock();
    if (!viewer || !viewer->stream_window_open_ || !viewer->last_focused_stream_ || !viewer->control_mouse_) {
      reject(); return;
    }
  }
  std::lock_guard commit_lock(pending_mutex_);
  if (pending->revision != clipboard_revision_.load() ||
      !session->transfer->Current(pending->token)) { reject(); return; }
  ClearFileOffer();
  Clipboard::FileOfferError error = Clipboard::FileOfferError::PublishFailed;
  if (!Clipboard::SetFileOffer(pending->offer, &error)) {
    LOG_WARN("Failed to publish clipboard file offer");
    // Reject metadata without starting a download, but explain why this
    // receiver cannot paste instead of leaving its user with a silent failure.
    pending->offer->Cancel();
    {
      std::lock_guard lock(file_progress_mutex_);
      const auto now = std::chrono::steady_clock::now();
      FileReceiveProgress notice;
      notice.id = ++next_file_progress_id_;
      notice.transfer.token = pending->token;
      notice.transfer.status = file_transfer::ReceiveStatus::Failed;
      notice.started = notice.updated = now;
      notice.offer_error = error;
      file_progress_ = notice;
      file_progress_session_ = session;
    }
    session->transfer->CancelReceive(pending->token);
    return;
  }
  ++clipboard_revision_;
  last_files_.clear();
  last_files_change_ = Clipboard::ChangeCount();
  last_text_.clear(); local_files_ = false;
  deferred_file_session_.reset(); deferred_local_session_.reset();
  file_offer_ = std::move(pending);
#endif
}

void ClipboardController::ClearFileOffer() {
#ifndef __OHOS__
  if (!file_offer_) return;
  if (file_offer_->offer->failed()) {
    std::lock_guard lock(file_progress_mutex_);
    if (file_progress_ && file_progress_->transfer.token == file_offer_->token &&
        file_progress_session_.lock() == file_offer_->session.lock()) {
      file_progress_->transfer.status = file_transfer::ReceiveStatus::Failed;
      file_progress_->updated = std::chrono::steady_clock::now();
    }
  }
  if (const auto session = file_offer_->session.lock())
    session->transfer->CancelReceive(file_offer_->token);
  file_offer_->offer->Cancel();
  Clipboard::ClearFileOffer();
  file_offer_.reset();
#endif
}

void ClipboardController::StopFiles() {
  ClearFileOffer();
  std::unordered_map<std::string, std::shared_ptr<FileSession>> sessions;
  { std::lock_guard lock(file_sessions_mutex_); sessions.swap(file_sessions_); }
  for (const auto& [_, session] : sessions) session->transfer->Stop();
  { std::lock_guard lock(pending_mutex_); pending_files_.reset(); }
  { std::lock_guard lock(file_progress_mutex_); file_progress_.reset(); file_progress_session_.reset(); }
}

std::optional<ClipboardController::FileReceiveProgress> ClipboardController::FileReceiveSnapshot() {
  std::lock_guard lock(file_progress_mutex_);
  return file_progress_;
}

void ClipboardController::CancelFileReceive(uint64_t id) {
  std::shared_ptr<FileSession> session;
  uint32_t token = 0;
  {
    std::lock_guard lock(file_progress_mutex_);
    if (!file_progress_ || file_progress_->id != id ||
        file_progress_->transfer.status != file_transfer::ReceiveStatus::Receiving) return;
    session = file_progress_session_.lock(); token = file_progress_->transfer.token;
  }
  if (file_offer_ && file_offer_->token == token && file_offer_->session.lock() == session)
    file_offer_->offer->Cancel();
  if (session) session->transfer->CancelReceive(token);
}

void ClipboardController::DismissFileReceive(uint64_t id) {
  std::lock_guard lock(file_progress_mutex_);
  if (!file_progress_ || file_progress_->id != id) return;
  dismissed_file_progress_id_ = id;
  file_progress_.reset(); file_progress_session_.reset();
}

}  // namespace crossdesk
