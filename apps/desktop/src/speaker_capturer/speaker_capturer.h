/*
 * @Author: DI JUNKUN
 * @Date: 2024-07-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SPEAKER_CAPTURER_H_
#define _SPEAKER_CAPTURER_H_

#include <functional>

namespace crossdesk {

class SpeakerCapturer {
 public:
  typedef std::function<void(unsigned char*, size_t, const char*)>
      speaker_data_cb;

 public:
  virtual ~SpeakerCapturer() {}

 public:
  virtual int Init(speaker_data_cb cb) = 0;
  virtual int Destroy() = 0;
  virtual int Start() = 0;
  virtual int Stop() = 0;
};
}  // namespace crossdesk
#endif