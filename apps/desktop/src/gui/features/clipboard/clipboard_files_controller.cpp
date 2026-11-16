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
          }, [this, remote, weak](std::shared_ptr<ClipboardFileTransfer::Files> files,
                                  uint64_t revision, uint32_t token) {
            if (!FileSessionActive(remote, weak.lock())) return;
            std::optional<PendingFiles> previous;
            {
              std::lock_guard lock(pending_mutex_);
              previous.swap(pending_files_);
              if (events_enabled_.load())
                pending_files_ = PendingFiles{remote, weak, std::move(files), revision, token};
            }
            if (previous) if (const auto old = previous->session.lock())
              old->transfer->Confirm(previous->token, false);
          }, [this, remote, weak, ids = std::unordered_map<uint32_t, uint32_t>{}]
              (uint32_t id, const std::string& name, uint64_t total, uint64_t bytes,
               file_transfer::Status status) mutable {
            auto [entry, added] = ids.try_emplace(id, 0);
            if (added) entry->second = file_transfer::Session::NextFileId();
            owner_.transfers_.UpdateClipboardSend(entry->second, remote, name, total, bytes, status,
                [weak] { if (const auto session = weak.lock()) session->transfer->Copy({}); });
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
      if (!viewer->tab_opened_ || !viewer->tab_selected_ || !viewer->control_mouse_) {
        if (deferred_file_session_.lock() == viewer) deferred_file_session_.reset();
        continue;
      }
      if (!viewer->clipboard_allowed_.load()) {
        if (changed && viewer->tab_opened_ && viewer->tab_selected_ && viewer->control_mouse_)
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
  std::optional<PendingFiles> pending;
  { std::lock_guard lock(pending_mutex_); pending.swap(pending_files_); }
  if (!pending) return;
  const auto session = pending->session.lock();
  const auto reject = [&] { if (session) session->transfer->Confirm(pending->token, false); };
  // Observe a local copy even when SDL has not delivered its notification yet.
  HandleLocalUpdate();
  if (!FileSessionActive(pending->remote, session)) { reject(); return; }
  if (session->controlling) {
    const auto viewer = session->viewer.lock();
    if (!viewer || !viewer->tab_opened_ || !viewer->tab_selected_ || !viewer->control_mouse_) {
      reject(); return;
    }
  }
  std::lock_guard commit_lock(pending_mutex_);
  if (pending->revision != clipboard_revision_.load() ||
      !session->transfer->Current(pending->token)) { reject(); return; }
  if (!Clipboard::SetFiles(pending->files->paths)) {
    LOG_WARN("Failed to publish received clipboard files");
    reject();
    return;
  }
  ++clipboard_revision_;
  last_files_ = pending->files->paths;
  last_files_change_ = Clipboard::ChangeCount();
  last_text_.clear(); local_files_ = false;
  deferred_file_session_.reset(); deferred_local_session_.reset();
  published_files_.push_back(std::move(pending->files));
  session->transfer->Confirm(pending->token, true);
#endif
}

void ClipboardController::StopFiles() {
  std::unordered_map<std::string, std::shared_ptr<FileSession>> sessions;
  { std::lock_guard lock(file_sessions_mutex_); sessions.swap(file_sessions_); }
  for (const auto& [_, session] : sessions) session->transfer->Stop();
  { std::lock_guard lock(pending_mutex_); pending_files_.reset(); }
  published_files_.clear();
}

}  // namespace crossdesk
