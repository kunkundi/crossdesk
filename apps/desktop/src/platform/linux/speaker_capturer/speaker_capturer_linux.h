/*
 * @Author: DI JUNKUN
 * @Date: 2025-07-15
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SPEAKER_CAPTURER_LINUX_H_
#define _SPEAKER_CAPTURER_LINUX_H_

#include <pulse/pulseaudio.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "speaker_capturer.h"

namespace crossdesk {

class SpeakerCapturerLinux : public SpeakerCapturer {
 public:
  SpeakerCapturerLinux();
  ~SpeakerCapturerLinux();

  int Init(speaker_data_cb cb) override;
  int Destroy() override;
  int Start() override;
  int Stop() override;

  int Pause();
  int Resume();

 private:
  std::string GetDefaultMonitorSourceName();
  void Cleanup();

 private:
  speaker_data_cb cb_ = nullptr;

  std::atomic<bool> inited_;
  std::atomic<bool> paused_;
  std::atomic<bool> stop_flag_;

  std::thread mainloop_thread_;
  pa_threaded_mainloop* mainloop_ = nullptr;
  pa_context* context_ = nullptr;
  pa_stream* stream_ = nullptr;

  std::mutex state_mtx_;
  std::vector<uint8_t> frame_cache_;
};
}  // namespace crossdesk
#endif