/*
 * @Author: DI JUNKUN
 * @Date: 2025-12-28
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CLIPBOARD_H_
#define _CLIPBOARD_H_

#include <functional>
#include <string>

namespace crossdesk {

class Clipboard {
 public:
  using OnClipboardChanged = std::function<int(const char* data, size_t size)>;

  Clipboard() = default;
  ~Clipboard() = default;

  static std::string GetText();

  static bool SetText(const std::string& text);

  static bool HasText();

  static void StartMonitoring(int check_interval_ms = 100,
                              OnClipboardChanged on_changed = nullptr);

  static void StopMonitoring();

  static bool IsMonitoring();
};

}  // namespace crossdesk

#endif