/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-24
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LAN_TRANSPORT_H_
#define _LAN_TRANSPORT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lan_identity.h"

namespace crossdesk::lan {
// One network thread; callbacks enqueue bounded value events for the UI-owned
// session manager. No network callback calls MiniRTC or destroys a Peer.
class Transport {
 public:
  struct Channel;
  using ChannelPtr = std::shared_ptr<Channel>;
  struct Event {
    enum Kind { Open, Message, Closed } kind;
    ChannelPtr channel;
    std::string payload;
  };
  Transport();
  ~Transport();
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;
  struct Listener {
    uint64_t id;
    unsigned port;
  };
  // Port zero lets the OS allocate and hold a free port on this address.
  Listener Listen(const Identity& identity, const std::string& address,
                  unsigned port);
  void StopAccept(uint64_t listener);
  ChannelPtr Connect(const DialEndpoint& target,
                     const std::string& expected_key);
  bool Send(const ChannelPtr& channel, std::string message,
            bool discard_on_close = false);
  void Authenticated(const ChannelPtr& channel);
  void Close(const ChannelPtr& channel);
  std::vector<Event> Poll();
  void Shutdown();
  struct Channel {
    uint64_t id = 0, listener = 0;
    bool incoming = false;
    // Published by Open, immutable thereafter.
    std::string local_address, remote_address, peer_fingerprint,
        dial_diagnostic;

   private:
    friend class Transport;
    struct State;
    std::shared_ptr<State> state;
  };

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace crossdesk::lan

#endif
