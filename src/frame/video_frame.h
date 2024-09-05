/*
 * @Author: DI JUNKUN
 * @Date: 2023-11-24
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_FRAME_H_
#define _VIDEO_FRAME_H_

#include <cstddef>
#include <cstdint>

class VideoFrame {
 public:
  VideoFrame();
  VideoFrame(size_t size);
  VideoFrame(size_t size, size_t width, size_t height);
  VideoFrame(const uint8_t *buffer, size_t size);
  VideoFrame(const uint8_t *buffer, size_t size, size_t width, size_t height);
  VideoFrame(const VideoFrame &video_frame);
  VideoFrame(VideoFrame &&video_frame);
  VideoFrame &operator=(const VideoFrame &video_frame);
  VideoFrame &operator=(VideoFrame &&video_frame);

  ~VideoFrame();

 public:
  const uint8_t *Buffer() { return buffer_; }
  size_t Size() { return size_; }

  uint8_t *GetBuffer() { return buffer_; }

  size_t GetWidth() { return width_; }
  size_t GetHeight() { return height_; }

 private:
  uint8_t *buffer_ = nullptr;
  size_t size_ = 0;
  size_t width_ = 0;
  size_t height_ = 0;
};

#endif