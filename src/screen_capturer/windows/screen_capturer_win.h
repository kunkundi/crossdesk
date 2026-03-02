/*
 * @Author: DI JUNKUN
 * @Date: 2026-02-27
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_WIN_H_
#define _SCREEN_CAPTURER_WIN_H_

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "screen_capturer.h"

namespace crossdesk {

class ScreenCapturerWin : public ScreenCapturer {
 public:
  ScreenCapturerWin();
  ~ScreenCapturerWin();

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
  std::unique_ptr<ScreenCapturer> impl_;
  int fps_ = 60;
  cb_desktop_data cb_;
  cb_desktop_data cb_orig_;

  std::unordered_map<void*, std::string> handle_to_canonical_;
  std::unordered_map<std::string, std::string> label_alias_;
  std::mutex alias_mutex_;
  std::vector<DisplayInfo> canonical_displays_;
  std::unordered_set<std::string> canonical_labels_;

  void BuildCanonicalFromImpl();
  void RebuildAliasesFromImpl();
};
}  // namespace crossdesk
#endif