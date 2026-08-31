/*
 * @Author: DI JUNKUN
 * @Date: 2024-08-15
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SPEAKER_CAPTURER_WASAPI_H_
#define _SPEAKER_CAPTURER_WASAPI_H_

#include "speaker_capturer.h"

namespace crossdesk {

class SpeakerCapturerWasapi : public SpeakerCapturer {
 public:
  SpeakerCapturerWasapi();
  ~SpeakerCapturerWasapi();

 public:
  virtual int Init(speaker_data_cb cb);
  virtual int Destroy();
  virtual int Start();
  virtual int Stop();

  int Pause();
  int Resume();

  speaker_data_cb GetCallback();

 private:
  speaker_data_cb cb_ = nullptr;

 private:
  bool inited_ = false;
};
}  // namespace crossdesk
#endif