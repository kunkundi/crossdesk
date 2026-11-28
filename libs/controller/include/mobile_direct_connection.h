/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-23
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOBILE_DIRECT_CONNECTION_H_
#define _MOBILE_DIRECT_CONNECTION_H_

#include <functional>
#include <memory>
#include <string>

#include "minirtc.h"

namespace crossdesk::mobile_controller {

// A controller borrows the platform's stream-configured peer for one outgoing
// session. Start/Stop run on the platform RTC queue. Status is callback-safe.
class DirectConnection {
 public:
  DirectConnection();
  ~DirectConnection();
  static std::string Endpoint(const std::string& input);
  static bool IsDomain(const std::string& input);
  using Notify =
      std::function<void(MiniRtcConnectionStatus, const std::string&)>;
  bool Start(MiniRtcPeerPtr* peer, const std::string& endpoint,
             const std::string& password, const std::string& expected_key,
             bool saved, Notify notify);
  bool Status(MiniRtcConnectionStatus status);
  std::string RemotePeerId() const;
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crossdesk::mobile_controller

#endif
