#ifndef CROSSDESK_GUI_FILE_TRANSFER_MANAGER_H_
#define CROSSDESK_GUI_FILE_TRANSFER_MANAGER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "file_transfer.h"
#include <file_browser.h>
#include "runtime/remote_session.h"

namespace crossdesk {

class GuiRuntime;
class PeerEventHandler;

class FileTransferManager {
 public:
  using FileTransferState = gui_detail::FileTransferState;
  using RemoteSession = gui_detail::RemoteSession;
  explicit FileTransferManager(GuiRuntime& owner);
  ~FileTransferManager();
  FileTransferState& global_state();
  FileTransferState& received_state();
  FileTransferState& state_for(const std::shared_ptr<RemoteSession>& props);
  // A reserved ID keeps a DeskHub notification stable while waiting for a slot.
  bool ProcessSelectedFile(const std::string& path, const std::shared_ptr<RemoteSession>& props,
                           const std::string& file_label, const std::string& remote_id = "",
                           uint32_t file_id = 0);
  void HandleAck(const char* data, size_t size, PeerEventHandler* handler, const std::string& remote);
  void Receive(const char* data, size_t size, PeerEventHandler* handler, const std::string& remote);
  void Connected(PeerEventHandler* handler, const std::string& remote);
  void Cancel(PeerEventHandler* handler, const std::string& remote = "");
  bool CancelSend(uint32_t file_id);
  void Poll();
  void Shutdown();
  FileBrowser& browser() { return browser_; }
  bool OpenBrowser(const std::string& remote);
  void SetBrowserSupport(PeerEventHandler* handler, const std::string& remote, bool supported);
  bool BrowserSupported(const std::string& remote);
  bool BrowserConnected(const std::string& remote);
  bool HandleBrowser(const char* data, size_t size, PeerEventHandler* handler,
                     const std::string& remote);
  std::vector<FileTransferState::FileTransferInfo> BrowserTransfers(const std::string& remote);
  void CancelBrowserTransfer(const std::string& remote, uint32_t file_id);

 private:
  struct Sender {
    std::shared_ptr<file_transfer::Session> session;
    std::shared_ptr<PeerEventHandler> handler;
    std::string remote;
  };
  struct Receiver {
    std::shared_ptr<FileReceiver> session;
    std::shared_ptr<PeerEventHandler> handler;
    std::filesystem::path default_directory;
  };
  void ProcessQueue(std::shared_ptr<RemoteSession> props);
  std::shared_ptr<FileReceiver> ReceiverFor(PeerEventHandler* handler, const std::string& remote);
  bool SendBrowser(const std::string& remote, const FileBrowser::Json& message);
  void UpdateProgress(FileTransferState& state,
                      const file_transfer::SendState& progress,
                      std::chrono::steady_clock::time_point now);
  void UpdateReceiveProgress(const file_transfer::ReceiveState& progress,
                             std::unordered_map<uint32_t, size_t>& notifications,
                             const std::string& remote_id);
  struct ReceiveRateSample {
    std::chrono::steady_clock::time_point updated;
    uint64_t bytes = 0;
  };
  std::mutex workers_mutex_, sessions_mutex_, wake_mutex_;
  std::condition_variable wake_;
  std::vector<std::future<void>> workers_;
  std::atomic<bool> stopping_{false};
  std::unordered_map<uint32_t, Sender> senders_;
  std::unordered_map<PeerEventHandler*, std::unordered_map<std::string, Receiver>> receivers_;
  std::unordered_map<PeerEventHandler*, std::set<std::string>> disconnected_;
  GuiRuntime& owner_;
  FileTransferState global_state_;
  // Keep received files separate from outgoing queues.
  FileTransferState received_state_;
  // Guarded by received_state_.file_transfer_list_mutex_.
  std::unordered_map<uint32_t, ReceiveRateSample> receive_rate_samples_;
  std::mutex browser_mutex_;
  std::map<std::string, std::shared_ptr<PeerEventHandler>> browser_peers_;
  std::set<std::string> browser_opened_;
  std::set<std::string> browser_supported_;
  FileBrowser browser_;
};

}  // namespace crossdesk

#endif
