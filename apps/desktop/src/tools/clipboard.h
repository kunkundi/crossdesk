/*
 * @Author: DI JUNKUN
 * @Date: 2025-12-28
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CLIPBOARD_H_
#define _CLIPBOARD_H_

#include "clipboard_file_promise.h"

#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crossdesk {

class Clipboard {
 public:
  using OnClipboardChanged = std::function<int(const char* data, size_t size)>;

  Clipboard() = default;
  ~Clipboard() = default;

  static std::string GetText();

  static bool SetText(const std::string& text);

  static bool HasText();

  // Main/UI thread only. File paths are UTF-8; a nonempty list takes precedence
  // over a file manager's accompanying text representation. No move/delete.
  static std::optional<std::vector<std::string>> GetFiles();
  static bool SetFiles(const std::vector<std::string>& paths);
  static uint64_t ChangeCount();
  // Publish metadata only. File contents are requested by an actual OS paste.
  static bool SetFileOffer(std::shared_ptr<ClipboardFilePromise> offer);
  static bool OwnsFileOffer(const std::shared_ptr<ClipboardFilePromise>& offer);
  static void ClearFileOffer();

  static void StartMonitoring(int check_interval_ms = 100,
                              OnClipboardChanged on_changed = nullptr);

  static void StopMonitoring();

  static bool IsMonitoring();
};

}  // namespace crossdesk

#endif
