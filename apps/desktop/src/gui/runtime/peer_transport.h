/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PEER_TRANSPORT_H_
#define _PEER_TRANSPORT_H_

#include <minirtc.h>
#include <nlohmann/json.hpp>
#include <functional>

namespace crossdesk {

// A managed GUI has a local endpoint instead of another signaling login.
// Viewer peers and direct LAN peers continue to use MiniRTC normally.
void BindLocalPeer(MiniRtcPeerPtr*, std::function<bool(nlohmann::json)> send);
void UnbindLocalPeer(MiniRtcPeerPtr*);
bool IsLocalPeer(MiniRtcPeerPtr*);
int PeerSendReliableDataFrameToPeer(MiniRtcPeerPtr*, const char*, size_t,
                                  const char*, const char*, size_t);
int PeerSendAudioFrameToPeer(MiniRtcPeerPtr*, const MiniRtcAudioFrame*,
                            const char*, const char*, size_t);
int PeerSendVideoFrameToPeer(MiniRtcPeerPtr*, const MiniRtcVideoFrame*,
                            const char*, const char*, size_t);
int PeerSendSignalMessage(MiniRtcPeerPtr*, const char*, size_t);
int PeerReconnectSignal(MiniRtcPeerPtr*);
int PeerDisconnectPeerConnection(MiniRtcPeerPtr*, const char*, const char*);
int PeerUpdateConnectionSettings(MiniRtcPeerPtr*, bool, bool, MiniRtcTurnMode);

}  // namespace crossdesk

#endif