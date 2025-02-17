/*
 * @Author: DI JUNKUN
 * @Date: 2025-02-17
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTCP_SENDER_H_
#define _RTCP_SENDER_H_

#include <memory>

#include "api/clock/clock.h"
#include "rtcp/rtcp_packet/rtcp_packet.h"

class RtcpSender {
 public:
  RtcpSender(std::shared_ptr<webrtc::Clock> clock, uint32_t ssrc);
  ~RtcpSender();

 public:
 private:
  std::shared_ptr<webrtc::Clock> clock_;
  uint32_t ssrc_;
}

#endif