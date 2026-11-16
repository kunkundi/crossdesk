/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CLIPBOARD_FILE_TRANSFER_H_
#define _CLIPBOARD_FILE_TRANSFER_H_

#include <file_transfer_core.h>
#include "clipboard_file_promise.h"
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

namespace crossdesk {

// One negotiated desktop connection. Disk IO and transfer pumping never run
// on the SDL or transport callback threads. Paths are never accepted from peers.
class ClipboardFileTransfer {
 public:
  using Files = ClipboardFilePromise::Files;
  using Send = std::function<bool(const std::string&)>;
  using Begin = std::function<std::optional<uint64_t>()>;
  using Offer = std::function<void(std::shared_ptr<ClipboardFilePromise>, uint64_t, uint32_t)>;
  using Progress = std::function<void(uint32_t, const std::string&, uint64_t, uint64_t,
                                     file_transfer::Status)>;
  struct ReceiveProgress {
    uint32_t token = 0;
    size_t files = 0, completed_files = 0;
    uint64_t total = 0, received = 0;
    std::string name;
    file_transfer::ReceiveStatus status = file_transfer::ReceiveStatus::Receiving;
  };
  using OnReceiveProgress = std::function<void(const ReceiveProgress&)>;
  ClipboardFileTransfer(Send send, Begin begin, Offer offer, Progress progress = {},
                        OnReceiveProgress receive_progress = {});
  ~ClipboardFileTransfer();
  void Copy(std::vector<std::string> paths);
  void Receive(const char* data, size_t size);
  void Paste(uint32_t token);
  void CancelReceive(uint32_t token);
  bool Current(uint32_t token) const { return current_receive_token_.load() == token; }
  void Stop();

 private:
  struct Item {
    uint32_t id;
    std::string path, name;
    uint64_t size;
    file_transfer::Status status = file_transfer::Status::Idle;
  };
  void Run();
  void StartCopy(const std::vector<std::string>& paths);
  void Handle(const std::string& packet);
  bool SendPacket(uint8_t type, uint32_t token, const char* data = nullptr, size_t size = 0);
  void ResetIncoming();
  void ResetOutgoing(file_transfer::Status status = file_transfer::Status::Cancelled);
  void ReportOutgoing();
  void StartNext();
  void StartReceive(uint32_t token);
  void FinishApplied();
  void FinishReceive(file_transfer::ReceiveStatus status);
  Send send_;
  Begin begin_;
  Offer offer_;
  Progress progress_;
  OnReceiveProgress receive_progress_;
  std::mutex mutex_, stop_mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  size_t queued_bytes_ = 0;
  std::deque<std::string> packets_;
  std::deque<uint32_t> cancellations_;
  std::optional<std::vector<std::string>> copy_;
  uint32_t paste_ = 0;
  std::thread worker_;
  std::atomic<uint32_t> current_receive_token_{0};
  // Worker thread only below.
  std::unique_ptr<file_transfer::Session> outgoing_, incoming_;
  std::vector<Item> items_;
  size_t next_ = 0;
  uint32_t send_token_ = 0, receive_token_ = 0, offered_send_token_ = 0;
  uint32_t latest_receive_token_ = 0, offered_token_ = 0;
  bool ready_ = false;
  bool applied_ = false;
  std::map<uint32_t, uint64_t> expected_;
  std::map<uint32_t, std::string> received_;
  std::map<uint32_t, uint64_t> received_bytes_;
  std::optional<ReceiveProgress> receiving_;
  std::vector<uint32_t> order_;
  uint32_t completed_id_ = 0;
  std::shared_ptr<Files> files_;
  std::shared_ptr<ClipboardFilePromise> offered_;
  std::chrono::steady_clock::time_point send_activity_, receive_activity_;
};

}  // namespace crossdesk

#endif
