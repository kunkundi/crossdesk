/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CLIPBOARD_FILE_PROMISE_H_
#define _CLIPBOARD_FILE_PROMISE_H_

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace crossdesk {

// Metadata can be queried freely. Only Request/Wait may fetch file contents.
// Native clipboard providers retain this object, never the GUI or a connection.
class ClipboardFilePromise {
 public:
  struct Entry {
    uint32_t id;
    std::string name;
    uint64_t size;
  };
  struct Files {
    std::filesystem::path directory;
    std::vector<std::string> paths;
    ~Files();
  };
  explicit ClipboardFilePromise(std::vector<Entry> entries) : entries_(std::move(entries)) {}
  const std::vector<Entry>& entries() const { return entries_; }
  void SetRequest(std::function<void()> request);
  bool Request();
  // Background/native transfer threads only. Cancel always wakes waiters.
  std::shared_ptr<Files> Wait();
  void Resolve(std::shared_ptr<Files> files);
  void Cancel();
  void Fail();
  bool failed() const;
  bool valid() const;

 private:
  const std::vector<Entry> entries_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::function<void()> request_;
  std::shared_ptr<Files> files_;
  bool requested_ = false, cancelled_ = false, failed_ = false;
};

}  // namespace crossdesk

#endif
