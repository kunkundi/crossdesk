#include "features/file_transfer/file_transfer_manager.h"

#include <algorithm>
#include <limits>
#include <thread>

#include "filesystem_utf8.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

FileTransferManager::FileTransferManager(GuiRuntime& owner)
    : owner_(owner), browser_(
        [this](const auto& remote, const auto& message) { return SendBrowser(remote, message); },
        [this](const auto& remote, const auto& path, uint32_t id) {
          if (!BrowserConnected(remote)) return false;
          auto props = owner_.FindRemoteSession(remote);
          return ProcessSelectedFile(path, props, owner_.file_label_, remote, id);
        },
        [this](const auto& remote, uint32_t id, const auto& directory) {
          if (!BrowserConnected(remote)) return false;
          std::shared_ptr<PeerEventHandler> events;
          { std::lock_guard lock(browser_mutex_); events = browser_peers_[remote]; }
          const auto receiver = ReceiverFor(events.get(), remote);
          return receiver && receiver->ReserveDirectory(id, directory);
        },
        [this](const auto& remote, uint32_t id) {
          if (!BrowserConnected(remote)) return false;
          for (const auto& info : BrowserTransfers(remote))
            if (!info.receiving && info.file_id == id) return CancelSend(id);
          return false;
        }) {}
FileTransferManager::~FileTransferManager() { Shutdown(); }

void FileTransferManager::Shutdown() {
  browser_.Stop();
  std::vector<std::future<void>> workers;
  {
    std::lock_guard lock(workers_mutex_);
    stopping_ = true;
    workers.swap(workers_);
  }
  {
    std::lock_guard lock(sessions_mutex_);
    for (auto& [id, sender] : senders_) sender.session->Cancel();
    for (auto& [handler, peers] : receivers_)
      for (auto& [id, receiver] : peers) receiver.session->Cancel();
    receivers_.clear();
  }
  wake_.notify_all();
  for (auto& worker : workers) worker.wait();
}

FileTransferManager::FileTransferState& FileTransferManager::global_state() { return global_state_; }
FileTransferManager::FileTransferState& FileTransferManager::received_state() { return received_state_; }
FileTransferManager::FileTransferState& FileTransferManager::state_for(const std::shared_ptr<RemoteSession>& props) {
  return props ? props->file_transfer_ : global_state_;
}

bool FileTransferManager::ProcessSelectedFile(const std::string& path,
    const std::shared_ptr<RemoteSession>& props, const std::string& file_label,
    const std::string& remote_id, uint32_t file_id) {
  if (path.empty() || stopping_ || (props && props->closing_)) return false;
  const auto file_path = PathFromUtf8(path);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file_path, ec)) return false;
  const auto size = std::filesystem::file_size(file_path, ec);
  if (ec) return false;
  auto& state = state_for(props);
  // Allocate once at enqueue time so queued and active notifications keep the
  // same identity, including repeated sends of the same path.
  const auto id = file_id ? file_id : file_transfer::Session::NextFileId();
  {
    std::lock_guard lock(state.file_transfer_list_mutex_);
    FileTransferState::FileTransferInfo info;
    const auto name = file_path.filename().u8string();
    info.file_name.assign(name.begin(), name.end());
    info.remote_id = props ? props->remote_id_ : remote_id;
    info.file_path = file_path; info.file_size = size;
    info.file_id = id;
    info.status = FileTransferState::FileTransferStatus::Queued;
    state.file_transfer_list_.push_back(std::move(info));
  }
  state.file_transfer_window_visible_ = true;
  {
    std::lock_guard lock(state.file_queue_mutex_);
    state.file_send_queue_.push({file_path, file_label, props ? props->remote_id_ : remote_id, id});
  }
  ProcessQueue(props);
  return true;
}

void FileTransferManager::ProcessQueue(std::shared_ptr<RemoteSession> props) {
  std::lock_guard workers_lock(workers_mutex_);
  auto& state = state_for(props);
  if (stopping_ || state.file_sending_) return;
  FileTransferState::QueuedFile file;
  {
    std::lock_guard lock(state.file_queue_mutex_);
    if (state.file_send_queue_.empty()) return;
    file = state.file_send_queue_.front(); state.file_send_queue_.pop();
    state.file_sending_ = true;
  }
  std::erase_if(workers_, [](auto& worker) {
    return worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
  workers_.push_back(std::async(std::launch::async, [this, props, file] {
    auto& state = state_for(props);
    const auto id = file.file_id;
    PeerPtr* peer;
    std::shared_ptr<PeerEventHandler> events;
    {
      std::shared_lock lock(owner_.remote_sessions_mutex_);
      peer = props ? props->peer_ : owner_.peer_;
      events = props ? props->peer_events_ : owner_.peer_events_;
    }
    if (!props && file.remote_id.rfind("lan/", 0) == 0) {
      events = owner_.ControllerEvents(file.remote_id);
      peer = events ? events->DirectPeer() : nullptr;
    }
    auto session = std::make_shared<file_transfer::Session>(
        [this, peer, events, file, props](const char*, const char* data, size_t size) {
          if (!peer || !events || (props && props->closing_) ||
              (!props && !owner_.IsAuthorizedController(file.remote_id))) return -1;
          auto callback = events->EnterCallback();
          if (!callback || !events->DataAllowed()) return -1;
          const auto wire_id = events->WireId(file.remote_id);
          return SendReliableDataFrameToPeer(
              peer, data, size, file.file_label.c_str(),
              wire_id.data(), wire_id.size());
        }, file_transfer::Session::Progress{});
    state.file_sent_bytes_ = 0; state.file_total_bytes_ = 0; state.file_send_rate_bps_ = 0;
    state.current_file_id_ = id;
    const auto started_at = std::chrono::steady_clock::now();
    auto next_rate_update = started_at + std::chrono::seconds(1);
    {
      std::lock_guard lock(state.file_transfer_mutex_);
      state.file_send_start_time_ = state.file_send_last_update_time_ = started_at;
      state.file_send_last_bytes_ = 0;
    }
    bool cancelled = false;
    {
      std::lock_guard lock(state.file_transfer_list_mutex_);
      for (auto& info : state.file_transfer_list_) {
        if (info.file_id == id) {
          cancelled = info.status == FileTransferState::FileTransferStatus::Cancelled;
          if (!cancelled) info.status = FileTransferState::FileTransferStatus::Sending;
          info.file_id = id; info.sent_bytes = 0;
          break;
        }
      }
    }
    const auto name = file.file_path.filename().u8string();
    bool started = false;
    {
      std::lock_guard lock(sessions_mutex_);
      const auto closed = disconnected_.find(events.get());
      const bool disconnected = closed != disconnected_.end() &&
          (closed->second.count("") || closed->second.count(file.remote_id));
      if (!cancelled && !stopping_ && peer && events && events->IsActive() && !disconnected &&
          (!props || !props->closing_) && !file.remote_id.empty()) {
        started = session->Start(file.file_path, std::string(name.begin(), name.end()),
                                 file_transfer::Session::Clock::now(), id);
        if (started) senders_.emplace(id, Sender{session, events, file.remote_id});
      }
    }
    uint64_t previous = UINT64_MAX;
    auto progress = file_transfer::SendState{id, 0, 0,
        cancelled ? file_transfer::Status::Cancelled : file_transfer::Status::Failed};
    if (cancelled && peer && events && (!props || !props->closing_) &&
        (props || owner_.IsAuthorizedController(file.remote_id))) {
      auto callback = events->EnterCallback();
      if (callback && events->DataAllowed()) {
        // A queued browser file already reserved a destination on the peer.
        // Release that reservation even though no data chunk was ever sent.
        const auto packet = EncodeFileCancel(id, 0);
        const auto wire_id = events->WireId(file.remote_id);
        SendReliableDataFrameToPeer(peer, packet.data(), packet.size(),
            file.file_label.c_str(), wire_id.data(), wire_id.size());
      }
    }
    if (started) {
      do {
        {
          std::lock_guard lock(state.file_transfer_list_mutex_);
          for (const auto& info : state.file_transfer_list_) if (info.file_id == id) {
            cancelled = info.status == FileTransferState::FileTransferStatus::Cancelling;
            break;
          }
        }
        if (cancelled) session->CancelSend();
        else if (stopping_ || !events->IsActive() || (props && props->closing_)) session->Cancel();
        const bool pumped = session->Pump();
        progress = session->Snapshot();
        const auto now = std::chrono::steady_clock::now();
        const bool rate_due = now >= next_rate_update;
        if (progress.acknowledged != previous || rate_due ||
            progress.status != file_transfer::Status::Sending) {
          UpdateProgress(state, progress, now);
          previous = progress.acknowledged;
          if (rate_due) next_rate_update = now + std::chrono::seconds(1);
        }
        if (progress.status != file_transfer::Status::Sending) break;
        if (!pumped) {
          std::unique_lock lock(wake_mutex_);
          wake_.wait_for(lock, std::chrono::milliseconds(5));
        }
      } while (true);
    }
    {
      std::lock_guard lock(sessions_mutex_);
      senders_.erase(id);
    }
    {
      std::lock_guard lock(state.file_transfer_list_mutex_);
      for (auto& info : state.file_transfer_list_) if (info.file_id == id) {
        const bool cancelled = progress.status == file_transfer::Status::Cancelled ||
            info.status == FileTransferState::FileTransferStatus::Cancelled ||
            info.status == FileTransferState::FileTransferStatus::Cancelling;
        info.status = progress.status == file_transfer::Status::Completed
            ? FileTransferState::FileTransferStatus::Completed
            : cancelled ? FileTransferState::FileTransferStatus::Cancelled
                        : FileTransferState::FileTransferStatus::Failed;
        info.rate_bps = 0;
        if (info.status == FileTransferState::FileTransferStatus::Completed) info.sent_bytes = info.file_size;
        break;
      }
    }
    if (progress.status != file_transfer::Status::Completed) {
      state.file_transfer_window_visible_ = false;
      state.file_sent_bytes_ = 0; state.file_total_bytes_ = 0;
      state.file_send_rate_bps_ = 0; state.current_file_id_ = 0;
    } else state.file_transfer_window_visible_ = true;
    state.file_sending_ = false;
    // Only the worker advances the queue, after all sends for this file drained.
    ProcessQueue(props);
  }));
}

void FileTransferManager::UpdateProgress(FileTransferState& state,
    const file_transfer::SendState& progress,
    std::chrono::steady_clock::time_point now) {
  state.file_sent_bytes_ = progress.acknowledged; state.file_total_bytes_ = progress.total;
  uint32_t rate = state.file_send_rate_bps_.load();
  {
    std::lock_guard lock(state.file_transfer_mutex_);
    const auto elapsed = now - state.file_send_last_update_time_;
    // Progress follows every ACK; speed uses one full sample per second for
    // this file. Sample even without new ACKs so a stalled transfer reads zero.
    if (elapsed >= std::chrono::seconds(1)) {
      const double seconds = std::chrono::duration<double>(elapsed).count();
      const auto bytes = progress.acknowledged >= state.file_send_last_bytes_
          ? progress.acknowledged - state.file_send_last_bytes_ : 0;
      const double measured = bytes * 8.0 / seconds;
      rate = static_cast<uint32_t>((std::min)(measured,
          static_cast<double>((std::numeric_limits<uint32_t>::max)())));
      state.file_send_rate_bps_ = rate;
      state.file_send_last_bytes_ = progress.acknowledged;
      state.file_send_last_update_time_ = now;
    }
  }
  std::lock_guard lock(state.file_transfer_list_mutex_);
  for (auto& info : state.file_transfer_list_) if (info.file_id == progress.id) {
    info.sent_bytes = progress.acknowledged; info.file_size = progress.total; info.rate_bps = rate;
    break;
  }
}

bool FileTransferManager::CancelSend(uint32_t file_id) {
  using Status = FileTransferState::FileTransferStatus;
  const auto cancel = [file_id](FileTransferState& state) {
    std::lock_guard lock(state.file_transfer_list_mutex_);
    for (auto& info : state.file_transfer_list_) {
      if (info.file_id != file_id || info.receiving) continue;
      if (info.status != Status::Queued && info.status != Status::Sending) return false;
      info.status = info.status == Status::Queued ? Status::Cancelled : Status::Cancelling;
      info.rate_bps = 0;
      return true;
    }
    return false;
  };
  bool cancelled = cancel(global_state_);
  if (!cancelled) {
    std::shared_lock lock(owner_.remote_sessions_mutex_);
    for (const auto& [_, props] : owner_.remote_sessions_) {
      if (props && cancel(props->file_transfer_)) { cancelled = true; break; }
    }
  }
  if (cancelled) wake_.notify_all();
  return cancelled;
}

void FileTransferManager::HandleAck(const char* data, size_t size,
    PeerEventHandler* handler, const std::string& remote) {
  FileTransferAck ack{};
  if (!DecodeFileTransferAck(data, size, &ack)) return;
  std::shared_ptr<file_transfer::Session> session;
  {
    std::lock_guard lock(sessions_mutex_);
    const auto it = senders_.find(ack.file_id);
    if (it == senders_.end() || it->second.handler.get() != handler || it->second.remote != remote) return;
    session = it->second.session;
  }
  session->Ack(data, size);
  wake_.notify_all();
}

void FileTransferManager::UpdateReceiveProgress(
    const file_transfer::ReceiveState& progress,
    std::unordered_map<uint32_t, size_t>& notifications,
    const std::string& remote_id) {
  using Status = FileTransferState::FileTransferStatus;
  std::lock_guard lock(received_state_.file_transfer_list_mutex_);
  auto& files = received_state_.file_transfer_list_;
  auto found = notifications.find(progress.id);
  if (found == notifications.end()) {
    FileTransferState::FileTransferInfo info;
    // Wire IDs can collide between peers or with outgoing transfers. Give each
    // reception a local notification identity from the same allocator as sends.
    info.file_id = file_transfer::Session::NextFileId();
    info.remote_id = remote_id;
    info.receiving = true;
    info.wire_file_id = progress.id;
    receive_rate_samples_.emplace(info.file_id,
        ReceiveRateSample{std::chrono::steady_clock::now(), 0});
    found = notifications.emplace(progress.id, files.size()).first;
    files.push_back(std::move(info));
  }
  auto& info = files[found->second];
  info.file_name = progress.name;
  info.file_size = progress.total;
  info.sent_bytes = progress.received;
  switch (progress.status) {
    case file_transfer::ReceiveStatus::Receiving: info.status = Status::Receiving; break;
    case file_transfer::ReceiveStatus::Completed: info.status = Status::Completed; break;
    case file_transfer::ReceiveStatus::Failed: info.status = Status::Failed; break;
    case file_transfer::ReceiveStatus::Cancelled: info.status = Status::Cancelled; break;
  }
  if (info.status != Status::Receiving) {
    info.rate_bps = 0;
    receive_rate_samples_.erase(info.file_id);
    notifications.erase(found);
  }
}

std::shared_ptr<FileReceiver> FileTransferManager::ReceiverFor(
    PeerEventHandler* handler, const std::string& remote) {
  const auto props = owner_.FindRemoteSession(remote);
  PeerPtr* peer;
  std::shared_ptr<PeerEventHandler> events;
  {
    std::shared_lock lock(owner_.remote_sessions_mutex_);
    peer = props ? props->peer_ : owner_.peer_;
    events = props ? props->peer_events_ : owner_.peer_events_;
  }
  if (!props && remote.rfind("lan/", 0) == 0) {
    events = owner_.ControllerEvents(remote);
    peer = events ? events->DirectPeer() : nullptr;
  }
  if (!peer || !events || events.get() != handler || (props && props->closing_)) return {};
  std::shared_ptr<FileReceiver> receiver;
  std::filesystem::path default_directory;
  {
    std::lock_guard lock(sessions_mutex_);
    const auto closed = disconnected_.find(handler);
    if (stopping_ || (closed != disconnected_.end() &&
        (closed->second.count("") || closed->second.count(remote)))) return {};
    auto& entry = receivers_[handler][remote];
    if (!entry.session) {
      entry.session = std::make_shared<FileReceiver>(); entry.handler = events;
      entry.default_directory = entry.session->OutputDir();
      // The transfer engine serializes progress callbacks for this receiver.
      entry.session->SetOnReceiveProgress(
          [this, remote, notifications = std::unordered_map<uint32_t, size_t>{}]
          (const file_transfer::ReceiveState& progress) mutable {
            UpdateReceiveProgress(progress, notifications, remote);
          });
      entry.session->SetOnSendAck([this, peer, events, remote, props](const FileTransferAck& ack) {
        if (!props && !owner_.IsAuthorizedController(remote)) return -1;
        auto callback = events->EnterCallback();
        if (!callback || !events->DataAllowed()) return -1;
        const auto bytes = EncodeFileTransferAck(ack);
        const auto wire_id = events->WireId(remote);
        return SendReliableDataFrameToPeer(
            peer, bytes.data(), bytes.size(), kFileFeedbackStream,
            wire_id.data(), wire_id.size());
      });
    }
    receiver = entry.session;
    default_directory = entry.default_directory;
  }
  const auto configured = owner_.config_center_->GetFileTransferSavePath();
  receiver->SetOutputDir(configured.empty() ? default_directory : PathFromUtf8(configured));
  return receiver;
}

void FileTransferManager::Receive(const char* data, size_t size,
    PeerEventHandler* handler, const std::string& remote) {
  if (auto receiver = ReceiverFor(handler, remote)) receiver->OnData(data, size);
}

void FileTransferManager::Connected(PeerEventHandler* handler, const std::string& remote) {
  std::lock_guard lock(sessions_mutex_);
  if (remote.empty()) disconnected_.erase(handler);
  else if (const auto it = disconnected_.find(handler); it != disconnected_.end()) {
    it->second.erase(remote);
    if (it->second.empty()) disconnected_.erase(it);
  }
}

void FileTransferManager::Cancel(PeerEventHandler* handler, const std::string& remote) {
  {
    std::lock_guard lock(browser_mutex_);
    for (auto it = browser_peers_.begin(); it != browser_peers_.end();) {
      if (it->second.get() == handler && (remote.empty() || it->first == remote)) {
        browser_.Disconnect(it->first);
        browser_opened_.erase(it->first);
        browser_supported_.erase(it->first);
        it = browser_peers_.erase(it);
      } else ++it;
    }
  }
  std::lock_guard lock(sessions_mutex_);
  disconnected_[handler].insert(remote);
  for (auto& [id, sender] : senders_)
    if (sender.handler.get() == handler && (remote.empty() || sender.remote == remote)) sender.session->Cancel();
  const auto it = receivers_.find(handler);
  if (it != receivers_.end()) {
    for (auto entry = it->second.begin(); entry != it->second.end();) {
      if (remote.empty() || entry->first == remote) {
        entry->second.session->Cancel(); entry = it->second.erase(entry);
      } else ++entry;
    }
    if (it->second.empty()) receivers_.erase(it);
  }
  wake_.notify_all();
}

void FileTransferManager::Poll() {
  std::vector<std::shared_ptr<FileReceiver>> receivers;
  {
    std::lock_guard lock(sessions_mutex_);
    for (const auto& [handler, peers] : receivers_)
      for (const auto& [remote, receiver] : peers) receivers.push_back(receiver.session);
  }
  for (const auto& receiver : receivers) receiver->Poll();
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(received_state_.file_transfer_list_mutex_);
  for (auto& info : received_state_.file_transfer_list_) {
    const auto sample = receive_rate_samples_.find(info.file_id);
    if (sample == receive_rate_samples_.end()) continue;
    const auto elapsed = now - sample->second.updated;
    if (elapsed < std::chrono::seconds(1)) continue;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const auto bytes = info.sent_bytes >= sample->second.bytes
        ? info.sent_bytes - sample->second.bytes : 0;
    info.rate_bps = static_cast<uint32_t>((std::min)(bytes * 8.0 / seconds,
        static_cast<double>((std::numeric_limits<uint32_t>::max)())));
    sample->second = {now, info.sent_bytes};
  }
}

void FileTransferManager::SetBrowserSupport(PeerEventHandler* handler,
    const std::string& remote, bool supported) {
  const auto props = owner_.FindRemoteSession(remote);
  const auto events = props ? props->peer_events_ : owner_.ControllerEvents(remote);
  if (!events || events.get() != handler || !events->DataAllowed()) return;
  std::lock_guard lock(browser_mutex_);
  if (const auto previous = browser_peers_.find(remote);
      previous != browser_peers_.end() && previous->second != events) {
    browser_.Disconnect(remote);
    browser_opened_.erase(remote);
  }
  browser_peers_[remote] = events;
  if (supported) browser_supported_.insert(remote);
  else browser_supported_.erase(remote);
}

bool FileTransferManager::BrowserSupported(const std::string& remote) {
  std::lock_guard lock(browser_mutex_);
  return browser_supported_.count(remote) != 0;
}

bool FileTransferManager::OpenBrowser(const std::string& remote) {
  auto props = owner_.FindRemoteSession(remote);
  auto events = props ? props->peer_events_ : owner_.ControllerEvents(remote);
  if (!events || !events->DataAllowed() || owner_.unattended_host_ ||
      (props ? props->closing_ || !props->connection_established_
             : !owner_.IsAuthorizedController(remote))) return false;
  std::lock_guard lock(browser_mutex_);
  if (const auto previous = browser_peers_.find(remote);
      previous != browser_peers_.end() && previous->second != events) {
    browser_.Disconnect(remote);
    browser_opened_.erase(remote);
    browser_supported_.erase(remote);
  }
  browser_peers_[remote] = events;
  // Opening the legacy file picker authorizes a send, not disk browsing.
  if (browser_supported_.count(remote)) browser_opened_.insert(remote);
  return true;
}

void FileTransferManager::CancelBrowserTransfer(const std::string& remote, uint32_t file_id) {
  for (const auto& info : BrowserTransfers(remote)) {
    if (info.file_id != file_id) continue;
    if (info.receiving) browser_.CancelTransfer(remote, info.wire_file_id);
    else CancelSend(file_id);
    return;
  }
}

bool FileTransferManager::BrowserConnected(const std::string& remote) {
  auto props = owner_.FindRemoteSession(remote);
  auto events = props ? props->peer_events_ : owner_.ControllerEvents(remote);
  if (!events || !events->DataAllowed() || owner_.unattended_host_ ||
      (props ? props->closing_ || !props->connection_established_
             : !owner_.IsAuthorizedController(remote))) return false;
  std::lock_guard lock(browser_mutex_);
  const auto it = browser_peers_.find(remote);
  return it != browser_peers_.end() && it->second == events;
}

bool FileTransferManager::SendBrowser(const std::string& remote, const FileBrowser::Json& message) {
  if (!BrowserConnected(remote)) return false;
  auto props = owner_.FindRemoteSession(remote);
  std::shared_ptr<PeerEventHandler> events;
  { std::lock_guard lock(browser_mutex_); events = browser_peers_[remote]; }
  if (!events) return false;
  auto callback = events->EnterCallback();
  if (!callback || !events->DataAllowed()) return false;
  PeerPtr* peer = events->IsDirect() ? events->DirectPeer() : props ? props->peer_ : owner_.peer_;
  const auto data = message.dump(-1, ' ', false, FileBrowser::Json::error_handler_t::replace);
  if (!peer || data.size() > 60000) return false;
  const auto wire_id = events->WireId(remote);
  return SendReliableDataFrameToPeer(peer, data.data(), data.size(),
      owner_.control_data_label_.c_str(), wire_id.data(), wire_id.size()) == 0;
}

bool FileTransferManager::HandleBrowser(const char* data, size_t size,
    PeerEventHandler* handler, const std::string& remote) {
  if (!data || size > 60000 || std::string_view(data, size).find("file_browser") == std::string_view::npos)
    return false;
  auto message = FileBrowser::Json::parse(data, data + size, nullptr, false);
  if (!message.is_object() || message.value("type", FileBrowser::Json{}) != "file_browser") return false;
  auto props = owner_.FindRemoteSession(remote);
  auto events = props ? props->peer_events_ : owner_.ControllerEvents(remote);
  if (owner_.unattended_host_ || !events || events.get() != handler || !events->DataAllowed() ||
      (props ? props->closing_ || !props->connection_established_
             : !owner_.IsAuthorizedController(remote))) return true;
  bool opened;
  {
    std::lock_guard lock(browser_mutex_);
    browser_peers_[remote] = events;
    if (message.value("op", FileBrowser::Json{}) == "presentation") {
      // Older browser builds synchronized window visibility. Ignore that
      // optional event: progress now belongs exclusively to the local sender.
      return true;
    }
    opened = browser_opened_.count(remote);
  }
  // A controlled computer cannot browse the controller's disk until that
  // controller explicitly opens this peer's file manager in this connection.
  const auto op = message.value("op", FileBrowser::Json{});
  if (props && !opened && op != "reply") {
    if (message.contains("id")) SendBrowser(remote,
        {{"op", "reply"}, {"id", message["id"]}, {"type", "file_browser"},
         {"version", 1}, {"ok", false}, {"error", "browser_not_open"}});
    return true;
  }
  browser_.Handle(remote, std::move(message));
  return true;
}

std::vector<FileTransferManager::FileTransferState::FileTransferInfo>
FileTransferManager::BrowserTransfers(const std::string& remote) {
  std::vector<FileTransferState::FileTransferInfo> result;
  const auto collect = [&](auto& state) {
    std::lock_guard lock(state.file_transfer_list_mutex_);
    for (const auto& info : state.file_transfer_list_)
      if (info.remote_id == remote) result.push_back(info);
  };
  collect(global_state_); collect(received_state_);
  if (auto props = owner_.FindRemoteSession(remote)) collect(props->file_transfer_);
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.file_id > b.file_id; });
  return result;
}

}  // namespace crossdesk
