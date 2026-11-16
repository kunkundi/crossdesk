/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CLIPBOARD_CONTROLLER_H_
#define _CLIPBOARD_CONTROLLER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include "clipboard.h"
#include "clipboard_file_transfer.h"

namespace crossdesk::gui_detail {
struct RemoteSession;
}

namespace crossdesk {

class GuiRuntime;
class PeerEventHandler;

// Owns clipboard synchronization state and keeps feedback-loop prevention out
// of the main UI coordinator.
class ClipboardController {
public:
  explicit ClipboardController(GuiRuntime &owner);
  ~ClipboardController();

  void SetEventType(uint32_t event_type);
  uint32_t event_type() const;

  void Initialize();
  void Shutdown();
  void QueueRemoteText(const char* data, size_t size,
                       const std::string& remote_id = {},
                       std::function<void(bool)> complete = {},
                       std::function<bool()> allowed = {});
  void ApplyPendingRemoteText();
  bool QueueRead(std::function<void(std::optional<std::string>)> complete);
  void HandleLocalUpdate(bool external_event = false);
  void ReceiveWeb(const char* data, size_t size, const std::string& remote_id,
                   PeerEventHandler* source);
  void ForgetWeb(const std::string& remote_id);
  void SetFileSupport(const std::string& remote_id, PeerEventHandler* source, bool supported);
  void ReceiveFiles(const char* data, size_t size, const std::string& remote_id,
                    PeerEventHandler* source);
  struct FileReceiveProgress {
    uint64_t id = 0;
    ClipboardFileTransfer::ReceiveProgress transfer;
    std::chrono::steady_clock::time_point started, updated;
    Clipboard::FileOfferError offer_error = Clipboard::FileOfferError::NoError;
  };
  std::optional<FileReceiveProgress> FileReceiveSnapshot();
  void CancelFileReceive(uint64_t id);
  void DismissFileReceive(uint64_t id);

private:
  struct WebSession;
  struct FileSession;
  bool FileSessionActive(const std::string& remote_id, const std::shared_ptr<FileSession>& session);
  void SendLocalFiles(bool changed);
  void ApplyPendingFiles();
  void StopFiles();
  void ClearFileOffer();
  void ApplyPendingWrite();
  bool WebSessionActive(const std::string& remote_id,
                         const std::shared_ptr<WebSession>& session);
  int SendToController(const std::string& remote_id, const std::string& text);
  int SendToPeers(const std::string &text);
  void SendDeferredLocalText();

  GuiRuntime &owner_;
  uint32_t event_type_ = 0;
  std::atomic<bool> events_enabled_{false};
  std::mutex pending_mutex_;
  std::deque<std::function<void(std::optional<std::string>)>> pending_reads_;
  std::optional<std::string> pending_remote_text_;
  std::string pending_remote_id_;
  std::weak_ptr<gui_detail::RemoteSession> pending_session_;
  uint64_t pending_generation_ = 0;
  std::function<void(bool)> pending_complete_;
  std::function<bool()> pending_allowed_;
  std::string last_text_;
  // UI thread only. Retain the destination of a local copy while its stream
  // window is unfocused; last_text_ holds the latest value to send on return.
  std::weak_ptr<gui_detail::RemoteSession> deferred_local_session_;
  std::mutex web_sessions_mutex_;
  std::unordered_map<std::string, std::shared_ptr<WebSession>> web_sessions_;
  std::mutex file_sessions_mutex_;
  std::unordered_map<std::string, std::shared_ptr<FileSession>> file_sessions_;
  std::atomic<uint64_t> clipboard_revision_{0};
  std::vector<std::string> last_files_;
  uint64_t last_files_change_ = 0;
  bool local_files_ = false;
  std::weak_ptr<gui_detail::RemoteSession> deferred_file_session_;
  struct PendingFiles {
    std::string remote;
    std::weak_ptr<FileSession> session;
    std::shared_ptr<ClipboardFilePromise> offer;
    uint64_t revision;
    uint32_t token;
  };
  std::optional<PendingFiles> pending_files_;
  std::optional<PendingFiles> file_offer_;
  std::mutex file_progress_mutex_;
  std::atomic<uint64_t> next_file_progress_id_{0};
  uint64_t dismissed_file_progress_id_ = 0;
  std::optional<FileReceiveProgress> file_progress_;
  std::weak_ptr<FileSession> file_progress_session_;
};

} // namespace crossdesk

#endif // _CLIPBOARD_CONTROLLER_H_
