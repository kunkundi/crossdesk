/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HISTORY_SERVER_ENDPOINT_H_
#define _HISTORY_SERVER_ENDPOINT_H_

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <string>

#include "minirtc.h"

namespace crossdesk {

// Use the transport parser so an embedded port takes precedence and a
// case-sensitive WebSocket route can identify a different signaling service.
inline std::string HistoryServerEndpoint(const std::string& address, int port) {
  MiniRtcSignalServerAddress parsed{};
  if (MiniRtcParseSignalServerAddress(address.data(), address.size(), &parsed) != 0)
    return "invalid/" + std::to_string(port) + "/" + address;
  std::string host(parsed.host);
  for (char& c : host)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  if (!host.empty() && host.back() == '.') host.pop_back();
  if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
    in6_addr address_bytes{};
    char normalized[INET6_ADDRSTRLEN]{};
    const auto literal = host.substr(1, host.size() - 2);
    if (inet_pton(AF_INET6, literal.c_str(), &address_bytes) == 1 &&
        inet_ntop(AF_INET6, &address_bytes, normalized, sizeof(normalized)))
      host = "[" + std::string(normalized) + "]";
  }
  return host + ":" + std::to_string(parsed.port ? parsed.port : port) + parsed.target;
}

}  // namespace crossdesk

#endif
