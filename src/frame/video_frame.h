/*
 * @Author: DI JUNKUN
 * @Date: 2023-11-24
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_FRAME_H_
#define _VIDEO_FRAME_H_

#include <cstddef>
#include <cstdint>

enum VideoFrameType {
  kEmptyFrame = 0,
  kVideoFrameKey = 3,
  kVideoFrameDelta = 4,
};

class VideoFrame {
 public:
  VideoFrame();
  VideoFrame(size_t size);
  VideoFrame(size_t size, uint32_t width, uint32_t height);
  VideoFrame(const uint8_t *buffer, size_t size);
  VideoFrame(const uint8_t *buffer, size_t size, uint32_t width,
             uint32_t height);
  VideoFrame(const VideoFrame &video_frame);
  VideoFrame(VideoFrame &&video_frame);
  VideoFrame &operator=(const VideoFrame &video_frame);
  VideoFrame &operator=(VideoFrame &&video_frame);

  ~VideoFrame();

 public:
  const uint8_t *Buffer() const { return buffer_; }
  size_t Size() const { return size_; }
  uint32_t Width() const { return width_; }
  uint32_t Height() const { return height_; }

  void SetSize(size_t size) { size_ = size; }
  void SetWidth(uint32_t width) { width_ = width; }
  void SetHeight(uint32_t height) { height_ = height; }

 private:
  uint8_t *buffer_ = nullptr;
  size_t size_ = 0;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
};

#endif