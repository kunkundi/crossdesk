/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PEER_EVENT_HANDLER_H_
#define _PEER_EVENT_HANDLER_H_

#include <cstddef>
#include <string>

#include "minirtc.h"

namespace crossdesk {

class GuiRuntime;

// Adapts MiniRTC C callbacks to the GUI runtime.
class PeerEventHandler {
public:
  explicit PeerEventHandler(GuiRuntime &owner);

  static void OnReceiveVideoBuffer(const MiniRtcVideoFrame *video_frame,
                                   const char *user_id, size_t user_id_size,
                                   const char *src_id, size_t src_id_size,
                                   void *user_data);
  static void OnReceiveAudioBuffer(const char *data, size_t size,
                                   const char *user_id, size_t user_id_size,
                                   const char *src_id, size_t src_id_size,
                                   void *user_data);
  static void OnReceiveDataBuffer(const char *data, size_t size,
                                  const char *user_id, size_t user_id_size,
                                  const char *src_id, size_t src_id_size,
                                  void *user_data);
  static void OnSignalStatus(SignalStatus status, const char *user_id,
                             size_t user_id_size, void *user_data);
  static void OnSignalMessage(const char *message, size_t size,
                              void *user_data);
  static void OnConnectionStatus(ConnectionStatus status, const char *user_id,
                                 size_t user_id_size, void *user_data);
  static void OnNetStatusReport(const char *client_id, size_t client_id_size,
                                TraversalMode mode,
                                const MiniRtcNetTrafficStats *net_traffic_stats,
                                const char *user_id, size_t user_id_size,
                                void *user_data);

private:
  static void SendClientInfo(PeerPtr *peer, const std::string &client_id);

  GuiRuntime &owner_;
};

} // namespace crossdesk

#endif