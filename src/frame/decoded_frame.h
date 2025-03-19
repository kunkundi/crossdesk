/*
 * @Author: DI JUNKUN
 * @Date: 2025-03-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DECODED_FRAME_H_
#define _DECODED_FRAME_H_

#include "video_frame.h"

class DecodedFrame : public VideoFrame {
 public:
  DecodedFrame(const uint8_t *buffer, size_t size, uint32_t width,
               uint32_t height)
      : VideoFrame(buffer, size, width, height) {}
  DecodedFrame(size_t size, uint32_t width, uint32_t height)
      : VideoFrame(size, width, height) {}
  DecodedFrame(const uint8_t *buffer, size_t size) : VideoFrame(buffer, size) {}
  DecodedFrame() = default;
  ~DecodedFrame() = default;

  int64_t ReceivedTimestamp() const { return received_timestamp_us_; }

  void SetReceivedTimestamp(int64_t received_timestamp_us) {
    received_timestamp_us_ = received_timestamp_us;
  }

  int64_t CapturedTimestamp() const { return captured_timestamp_us_; }

  void SetCapturedTimestamp(int64_t captured_timestamp_us) {
    captured_timestamp_us_ = captured_timestamp_us;
  }

  int64_t DecodedTimestamp() const { return decoded_timestamp_us_; }

  void SetDecodedTimestamp(int64_t decoded_timestamp_us) {
    decoded_timestamp_us_ = decoded_timestamp_us;
  }

  uint32_t DecodedWidth() const { return decoded_width_; }

  void SetDecodedWidth(uint32_t decoded_width) {
    decoded_width_ = decoded_width;
  }

  uint32_t decodedHeight() const { return decoded_height_; }

  void SetdecodedHeight(uint32_t decoded_height) {
    decoded_height_ = decoded_height;
  }

 private:
  int64_t received_timestamp_us_ = 0;
  int64_t captured_timestamp_us_ = 0;
  int64_t decoded_timestamp_us_ = 0;
  uint32_t decoded_width_ = 0;
  uint32_t decoded_height_ = 0;
};

#endif