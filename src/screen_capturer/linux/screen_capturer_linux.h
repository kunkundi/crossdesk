/*
 * @Author: DI JUNKUN
 * @Date: 2026-03-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_LINUX_H_
#define _SCREEN_CAPTURER_LINUX_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "screen_capturer.h"

namespace crossdesk {

class ScreenCapturerLinux : public ScreenCapturer {
 public:
  ScreenCapturerLinux();
  ~ScreenCapturerLinux();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  int Destroy() override;
  int Start(bool show_cursor) override;
  int Stop() override;

  int Pause(int monitor_index) override;
  int Resume(int monitor_index) override;

  int SwitchTo(int monitor_index) override;
  int ResetToInitialMonitor() override;

  std::vector<DisplayInfo> GetDisplayInfoList() override;

 private:
  enum class BackendType { kNone, kX11, kDrm, kWayland };

 private:
  int InitX11();
  int InitDrm();
  int InitWayland();
  bool TryFallbackToDrm(bool show_cursor);
  bool TryFallbackToX11(bool show_cursor);
  bool TryFallbackToWayland(bool show_cursor);
  void UpdateAliasesFromBackend(ScreenCapturer* backend);
  std::string MapDisplayName(const char* display_name) const;

 private:
  std::unique_ptr<ScreenCapturer> impl_;
  BackendType backend_ = BackendType::kNone;
  int fps_ = 60;
  cb_desktop_data callback_;
  cb_desktop_data callback_orig_;
  std::vector<DisplayInfo> canonical_displays_;
  mutable std::mutex alias_mutex_;
  std::unordered_map<std::string, std::string> label_alias_;
};

}  // namespace crossdesk

#endif
