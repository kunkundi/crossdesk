/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "runtime/peer_transport.h"

#ifdef _WIN32
#include <map>
#include <mutex>
#endif

namespace crossdesk {
namespace {
using Json = nlohmann::json;
using LocalSender = std::function<bool(Json)>;
Json Bytes(const char* data, size_t size) {
  return Json::binary(std::vector<uint8_t>(data, data + size));
}
#ifdef _WIN32
std::mutex routes_mutex;
std::map<MiniRtcPeerPtr*, LocalSender> routes;
LocalSender FindLocalSender(MiniRtcPeerPtr* peer) {
  std::lock_guard lock(routes_mutex);
  const auto found = routes.find(peer);
  return found == routes.end() ? LocalSender{} : found->second;
}
#else
LocalSender FindLocalSender(MiniRtcPeerPtr*) { return {}; }
#endif
}
#ifdef _WIN32
void BindLocalPeer(MiniRtcPeerPtr* peer, std::function<bool(Json)> send) {
  std::lock_guard lock(routes_mutex);
  routes[peer] = std::move(send);
}
void UnbindLocalPeer(MiniRtcPeerPtr* peer) {
  std::lock_guard lock(routes_mutex);
  routes.erase(peer);
}
bool IsLocalPeer(MiniRtcPeerPtr* peer) {
  std::lock_guard lock(routes_mutex);
  return routes.contains(peer);
}
#else
void BindLocalPeer(MiniRtcPeerPtr*, std::function<bool(Json)>) {}
void UnbindLocalPeer(MiniRtcPeerPtr*) {}
bool IsLocalPeer(MiniRtcPeerPtr*) { return false; }
#endif

int PeerSendReliableDataFrameToPeer(MiniRtcPeerPtr* peer, const char* data,
    size_t size, const char* stream, const char* id, size_t id_size) {
  if (auto send = FindLocalSender(peer)) {
    if (!data || !size || !stream || !id || !id_size) return -1;
    return send({{"op", "data"}, {"id", std::string(id, id_size)},
                 {"stream", stream}, {"bytes", Bytes(data, size)}}) ? 0 : -1;
  }
  return MiniRtcSendReliableDataFrameToPeer(peer, data, size, stream, id, id_size);
}
int PeerSendAudioFrameToPeer(MiniRtcPeerPtr* peer, const MiniRtcAudioFrame* frame,
    const char* stream, const char* id, size_t size) {
  if (auto send = FindLocalSender(peer)) {
    if (!frame || !frame->data || !frame->size || !stream || !id || !size) return -1;
    return send({{"op", "audio"}, {"id", std::string(id, size)},
        {"stream", stream}, {"bytes", Bytes(frame->data, frame->size)}}) ? 0 : -1;
  }
  return MiniRtcSendAudioFrameToPeer(peer, frame, stream, id, size);
}
int PeerSendVideoFrameToPeer(MiniRtcPeerPtr* peer, const MiniRtcVideoFrame* frame,
    const char* stream, const char* id, size_t size) {
  // The persistent host owns video. A GUI capturing for a direct LAN viewer
  // must not send that second capture into its mirrored cloud connections.
  return IsLocalPeer(peer) ? 0 : MiniRtcSendVideoFrameToPeer(peer, frame, stream, id, size);
}
int PeerSendSignalMessage(MiniRtcPeerPtr* peer, const char* data, size_t size) {
  if (auto send = FindLocalSender(peer)) {
    if (!data || !size) return -1;
    return send({{"op", "signal"}, {"message", std::string(data, size)}}) ? 0 : -1;
  }
  return MiniRtcSendSignalMessage(peer, data, size);
}
int PeerReconnectSignal(MiniRtcPeerPtr* peer) {
  if (auto send = FindLocalSender(peer)) return send({{"op", "reconnect"}}) ? 0 : -1;
  return MiniRtcReconnectSignal(peer);
}
int PeerDisconnectPeerConnection(MiniRtcPeerPtr* peer, const char* host, const char* id) {
  if (auto send = FindLocalSender(peer))
    return send({{"op", "disconnect"}, {"id", id}}) ? 0 : -1;
  return MiniRtcDisconnectPeerConnection(peer, host, id);
}
int PeerUpdateConnectionSettings(MiniRtcPeerPtr* peer, bool hardware, bool av1,
                                  MiniRtcTurnMode turn) {
  if (auto send = FindLocalSender(peer))
    return send({{"op", "connection_settings"},
          {"av1", av1}, {"turn", static_cast<int>(turn)}}) ? 0 : -1;
  return MiniRtcUpdateConnectionSettings(peer, hardware, av1, turn);
}
}  // namespace crossdesk
