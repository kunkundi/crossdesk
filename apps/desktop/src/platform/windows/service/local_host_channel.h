/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LOCAL_HOST_CHANNEL_H_
#define _LOCAL_HOST_CHANNEL_H_

#include <windows.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace crossdesk {

// Authenticated, local-only link between the persistent SYSTEM host and the
// console user's GUI. Losing this link never tears down a remote RTC session.
// Application messages are consumed on the application's main thread.
class LocalHostChannel {
 public:
  explicit LocalHostChannel(bool server);
  ~LocalHostChannel();
  bool Send(const nlohmann::json& message);
  // Fail pending user-session operations and reconnect without closing RTC.
  void Reconnect();
  std::vector<nlohmann::json> Receive();

 private:
  void Run();
  bool Authenticate(HANDLE pipe, std::string& reason) const;
  bool Read(HANDLE pipe, HANDLE event, void* bytes, DWORD size);
  bool QueueIncoming(nlohmann::json message, size_t size);
  void SetConnected(bool connected);
  const bool server_;
  DWORD session_ = 0;
  HANDLE stop_ = nullptr;
  std::thread worker_;
  std::atomic<bool> connected_{false};
  std::mutex mutex_;
  std::deque<std::vector<uint8_t>> outgoing_;
  std::vector<nlohmann::json> incoming_;
  size_t outgoing_bytes_ = 0, incoming_bytes_ = 0;
};

}  // namespace crossdesk

#endif