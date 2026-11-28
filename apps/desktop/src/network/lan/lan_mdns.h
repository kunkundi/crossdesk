/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-27
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LAN_MDNS_H_
#define _LAN_MDNS_H_

#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <functional>
#include <string>
#include <vector>

namespace crossdesk::lan {
using LocalNameResult =
    std::function<void(std::vector<asio::ip::address>, bool complete)>;
#if defined(_WIN32) || \
    (defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__))
inline constexpr bool kUseMulticastFallback = true;
// Start and cancel on the transport I/O thread. Completion is asynchronous.
std::function<void()> QueryLocalName(asio::io_context& io,
                                     const std::string& host,
                                     LocalNameResult result);
#else
inline constexpr bool kUseMulticastFallback = false;
inline std::function<void()> QueryLocalName(asio::io_context&,
                                            const std::string&,
                                            LocalNameResult) {
  return {};
}
#endif
}  // namespace crossdesk::lan

#endif
