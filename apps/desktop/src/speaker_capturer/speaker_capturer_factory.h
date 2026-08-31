/*
 * @Author: DI JUNKUN
 * @Date: 2024-07-22
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SPEAKER_CAPTURER_FACTORY_H_
#define _SPEAKER_CAPTURER_FACTORY_H_

#include "speaker_capturer.h"

namespace crossdesk {

class SpeakerCapturerFactory {
 public:
  virtual ~SpeakerCapturerFactory() {}

 public:
  SpeakerCapturer* Create();
};
}  // namespace crossdesk
#endif
