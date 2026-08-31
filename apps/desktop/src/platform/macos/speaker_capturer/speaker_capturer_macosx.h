/*
 * @Author: DI JUNKUN
 * @Date: 2024-08-02
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SPEAKER_CAPTURER_MACOSX_H_
#define _SPEAKER_CAPTURER_MACOSX_H_

#include <thread>
#include <vector>

#include "speaker_capturer.h"

namespace crossdesk {

class SpeakerCapturerMacosx : public SpeakerCapturer {
 public:
  SpeakerCapturerMacosx();
  ~SpeakerCapturerMacosx();

 public:
  virtual int Init(speaker_data_cb cb);
  virtual int Destroy();
  virtual int Start();
  virtual int Stop();

  int Pause();
  int Resume();

 public:
  speaker_data_cb cb_ = nullptr;
  bool inited_ = false;

  class Impl;
  Impl* impl_ = nullptr;
};
}  // namespace crossdesk
#endif