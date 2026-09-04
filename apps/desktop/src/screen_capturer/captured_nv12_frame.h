/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CAPTURED_NV12_FRAME_H_
#define _CAPTURED_NV12_FRAME_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "minirtc.h"

namespace crossdesk {

class CapturedNv12Frame;

// Owns the pixels behind captured native frames. A frame may outlive the
// capture callback while MiniRTC waits for its asynchronous encoder, so capture
// buffers cannot be borrowed directly. Released allocations are cached to keep
// that ownership transfer inexpensive after the pipeline is warm.
class CapturedNv12FramePool final
    : public std::enable_shared_from_this<CapturedNv12FramePool> {
 public:
  static std::shared_ptr<CapturedNv12FramePool> Create(
      size_t max_cached_buffers = 4);

  CapturedNv12Frame* CopyFrom(const uint8_t* data, size_t size,
                              uint32_t width, uint32_t height);

 private:
  explicit CapturedNv12FramePool(size_t max_cached_buffers);
  void Recycle(std::vector<uint8_t> storage);

  friend class CapturedNv12Frame;

  const size_t max_cached_buffers_;
  std::mutex mutex_;
  std::vector<std::vector<uint8_t>> free_buffers_;
};

class CapturedNv12Frame final {
 public:
  const MiniRtcNativeVideoFrame* Descriptor() const { return &descriptor_; }
  size_t Size() const { return storage_.size(); }

  void AddRef();
  void Release();

 private:
  friend class CapturedNv12FramePool;

  CapturedNv12Frame(std::shared_ptr<CapturedNv12FramePool> pool,
                    std::vector<uint8_t> storage, uint32_t width,
                    uint32_t height);
  ~CapturedNv12Frame();

  static void RetainOwner(void* owner);
  static void ReleaseOwner(void* owner);
  static int CopyToNv12(void* owner, uint8_t* destination,
                        size_t destination_size);

  std::atomic<uint32_t> references_{1};
  std::shared_ptr<CapturedNv12FramePool> pool_;
  std::vector<uint8_t> storage_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  MiniRtcNativeVideoFrame descriptor_{};
};

}  // namespace crossdesk

#endif
