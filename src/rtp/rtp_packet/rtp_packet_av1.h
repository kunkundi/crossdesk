/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-23
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RTP_PACKET_AV1_H_
#define _RTP_PACKET_AV1_H_

#include "rtp_packet.h"

class RtpPacketAv1 : public RtpPacket {
 public:
  RtpPacketAv1();
  virtual ~RtpPacketAv1();

 public:
  bool GetFrameHeaderInfo();

  bool Av1FrameStart() {
    // return !z_ && !y_;

    if (z_ == 0 && y_ == 0 && w_ == 1) {
      return true;
    } else if (z_ == 0 && y_ == 1 && w_ == 1) {
      return true;
    } else {
      return false;
    }
  }

  bool Av1FrameEnd() {
    // return z_ && !y_;

    if (z_ == 0 && y_ == 0 && w_ == 1) {
      return true;
    } else if (z_ == 1 && y_ == 0 && w_ == 1) {
      return true;
    } else {
      return false;
    }
  }

 private:
  uint8_t av1_aggr_header_ = 0;
  uint8_t z_ = 0;
  uint8_t y_ = 0;
  uint8_t w_ = 0;
  uint8_t n_ = 0;
};

#endif