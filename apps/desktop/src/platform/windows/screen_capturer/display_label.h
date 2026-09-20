/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef CROSSDESK_DISPLAY_LABEL_H_
#define CROSSDESK_DISPLAY_LABEL_H_

#include <Windows.h>

#include <string>

namespace crossdesk {

inline std::string WideToUtf8(const std::wstring& wstr) {
  if (wstr.empty()) return {};
  const int size_needed =
      WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                          nullptr, 0, nullptr, nullptr);
  if (size_needed <= 0) return {};
  std::string result(static_cast<size_t>(size_needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                      result.data(), size_needed, nullptr, nullptr);
  return result;
}

// "\\.\DISPLAY1" -> "DISPLAY1": the label every Windows backend reports so
// the wrapper can alias older plugins that identify streams by name.
inline std::string GetDisplayLabel(const std::wstring& wide_name) {
  std::string name = WideToUtf8(wide_name);
  constexpr char kDevicePrefix[] = "\\\\.\\";
  if (name.rfind(kDevicePrefix, 0) == 0) {
    name.erase(0, sizeof(kDevicePrefix) - 1);
  }
  return name;
}

}  // namespace crossdesk

#endif  // CROSSDESK_DISPLAY_LABEL_H_
