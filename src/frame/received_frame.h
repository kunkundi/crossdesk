/*
 * @Author: DI JUNKUN
 * @Date: 2025-03-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RECEIVED_FRAME_H_
#define _RECEIVED_FRAME_H_

#include "video_frame.h"

class ReceivedFrame : public VideoFrame {
 public:
  ReceivedFrame(const uint8_t *buffer, size_t size)
      : VideoFrame(buffer, size) {}
  ReceivedFrame() = default;
  ~ReceivedFrame() = default;

  int64_t ReceivedTimestamp() const { return received_timestamp_us_; }

  void SetReceivedTimestamp(int64_t received_timestamp_us) {
    received_timestamp_us_ = received_timestamp_us;
  }

  int64_t CapturedTimestamp() const { return captured_timestamp_us_; }

  void SetCapturedTimestamp(int64_t captured_timestamp_us) {
    captured_timestamp_us_ = captured_timestamp_us;
  }

 private:
  int64_t received_timestamp_us_ = 0;
  int64_t captured_timestamp_us_ = 0;
};

#endif