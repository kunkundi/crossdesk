/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-10
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_BAND_GUARD_H_
#define _PRIVACY_BAND_GUARD_H_

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

#include "privacy_band_ipc.h"

namespace crossdesk {

struct PrivacyScreenText;

// Lifecycle and snapshots are owned by the privacy control thread. The broker
// owns all high-band HWND operations. This class never injects an existing
// process and never terminates a process found by name or PID.
class PrivacyBandGuard {
 public:
  ~PrivacyBandGuard();
  static bool Available(std::string& error);
  bool Start(const PrivacyScreenText& text, std::string& error);
  void UpdateCoverStyle(const PrivacyScreenText& text);
  bool Stop(std::string& error);
  bool Healthy(std::string& error);
  bool Windows(std::vector<PrivacyBandWindow>& windows, std::string& error);
  bool stopped() const { return !process_; }
  DWORD process_id() const { return process_ ? GetProcessId(process_) : 0; }

 private:
  HANDLE process_ = nullptr;
  HANDLE job_ = nullptr;
  HANDLE stop_ = nullptr;
  HANDLE ready_ = nullptr;
  HANDLE mapping_ = nullptr;
  HANDLE image_lock_ = nullptr;
  HANDLE dll_lock_ = nullptr;
  PrivacyBandShared* shared_ = nullptr;
  std::filesystem::path broker_path_;
  std::filesystem::path broker_directory_;
};

}  // namespace crossdesk

#endif  // _PRIVACY_BAND_GUARD_H_
