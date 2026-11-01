/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PEER_EVENT_HANDLER_H_
#define _PEER_EVENT_HANDLER_H_

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "minirtc.h"
#include "runtime/session_lifecycle.h"
#include "runtime/device_login.h"

namespace crossdesk {

class GuiRuntime;
namespace lan {
struct Session;
}

// Adapts MiniRTC C callbacks to the GUI runtime.
class PeerEventHandler {
public:
  explicit PeerEventHandler(GuiRuntime &owner);

  void BindDirect(const std::shared_ptr<lan::Session>& session) {
    direct_ = session;
    direct_bound_ = true;
  }
  std::string ConnectionKey(const char* id, size_t size) const;
  std::string WireId(const std::string& key) const;
  bool DataAllowed() const;
  bool IsDirect() const { return direct_bound_; }
  void SetDirectPeer(PeerPtr* peer) { direct_peer_ = peer; }
  PeerPtr* DirectPeer() const { return direct_peer_; }
  void FlushEarlyData();
  void ApplyConnectionStatus(ConnectionStatus status, const std::string& id) {
    DispatchConnectionStatus(status, id.data(), id.size(), this);
  }
  void StopAccepting() { callbacks_.StopAccepting(); }
  bool IsActive() const { return callbacks_.IsActive(); }
  auto EnterCallback() { return callbacks_.Enter(); }
  void Deactivate() { callbacks_.Close(); }

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
  static int OnSignalExchange(SignalExchangeStage stage, const char* message,
                              size_t size, char* response, size_t capacity,
                              void* user_data);
  static void OnConnectionStatus(ConnectionStatus status, const char *user_id,
                                 size_t user_id_size, void *user_data);
  static void OnNetStatusReport(const char *client_id, size_t client_id_size,
                                TraversalMode mode,
                                const MiniRtcNetTrafficStats *net_traffic_stats,
                                const char *user_id, size_t user_id_size,
                                void *user_data);

private:
  static void DispatchConnectionStatus(ConnectionStatus status, const char* id,
                                       size_t size, void* context);
  static void SendClientInfo(PeerPtr* peer, const std::string& client_id);
  bool QueueEarlyData(const char* data, size_t size, const char* label,
                      size_t label_size);
  std::weak_ptr<lan::Session> direct_;
  bool direct_bound_ = false;
  PeerPtr* direct_peer_ = nullptr;
  std::mutex early_data_mutex_;
  std::vector<std::string> early_data_;
  size_t early_data_bytes_ = 0;

  GuiRuntime& owner_;
  DeviceLogin device_login_;
  gui_detail::SessionCallbackGate callbacks_;
};

} // namespace crossdesk

#endif
