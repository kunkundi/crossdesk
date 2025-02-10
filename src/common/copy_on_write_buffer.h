/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-22
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _COPY_ON_WRITE_BUFFER_H_
#define _COPY_ON_WRITE_BUFFER_H_

#include <memory>
#include <vector>

class CopyOnWriteBuffer {
 public:
  CopyOnWriteBuffer() = default;
  CopyOnWriteBuffer(size_t size) {
    buffer_ = std::make_shared<std::vector<uint8_t>>(size);
  }
  CopyOnWriteBuffer(const CopyOnWriteBuffer& other) = default;
  CopyOnWriteBuffer(CopyOnWriteBuffer&& other) noexcept = default;
  CopyOnWriteBuffer& operator=(const CopyOnWriteBuffer& other) = default;
  CopyOnWriteBuffer& operator=(CopyOnWriteBuffer&& other) noexcept = default;

  void SetData(const uint8_t* data, size_t size) {
    buffer_ = std::make_shared<std::vector<uint8_t>>(data, data + size);
  }

  const uint8_t* data() const { return buffer_ ? buffer_->data() : nullptr; }

  size_t size() const { return buffer_ ? buffer_->size() : 0; }

  uint8_t& operator[](size_t index) {
    EnsureUnique();
    return (*buffer_)[index];
  }

  const uint8_t& operator[](size_t index) const { return (*buffer_)[index]; }

 private:
  void EnsureUnique() {
    if (buffer_.use_count() != 1) {
      buffer_ = std::make_shared<std::vector<uint8_t>>(*buffer_);
    }
  }

  std::shared_ptr<std::vector<uint8_t>> buffer_;
};

#endif