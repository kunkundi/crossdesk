/*
 * @Author: DI JUNKUN
 * @Date: 2025-03-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ENCODED_FRAME_H_
#define _ENCODED_FRAME_H_

#include "video_frame.h"

class EncodedFrame : public VideoFrame {
 public:
  EncodedFrame(const uint8_t *buffer, size_t size, uint32_t width,
               uint32_t height)
      : VideoFrame(buffer, size, width, height) {}
  EncodedFrame(size_t size, uint32_t width, uint32_t height)
      : VideoFrame(size, width, height) {}
  EncodedFrame(const uint8_t *buffer, size_t size) : VideoFrame(buffer, size) {}
  EncodedFrame() = default;
  ~EncodedFrame() = default;

  int64_t CapturedTimestamp() const { return captured_timestamp_us_; }

  void SetCapturedTimestamp(int64_t captured_timestamp_us) {
    captured_timestamp_us_ = captured_timestamp_us;
  }

  int64_t EncodedTimestamp() const { return encoded_timestamp_us_; }

  void SetEncodedTimestamp(int64_t encoded_timestamp_us) {
    encoded_timestamp_us_ = encoded_timestamp_us;
  }

  VideoFrameType FrameType() const { return frame_type_; }

  void SetFrameType(VideoFrameType frame_type) { frame_type_ = frame_type; }

  uint32_t EncodedWidth() const { return encoded_width_; }

  void SetEncodedWidth(uint32_t encoded_width) {
    encoded_width_ = encoded_width;
  }

  uint32_t EncodedHeight() const { return encoded_height_; }

  void SetEncodedHeight(uint32_t encoded_height) {
    encoded_height_ = encoded_height;
  }

 private:
  int64_t captured_timestamp_us_ = 0;
  int64_t encoded_timestamp_us_ = 0;
  VideoFrameType frame_type_ = VideoFrameType::kVideoFrameDelta;
  uint32_t encoded_width_ = 0;
  uint32_t encoded_height_ = 0;
};

#endif