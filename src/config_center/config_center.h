/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONFIG_CENTER_H_
#define _CONFIG_CENTER_H_

class ConfigCenter {
 public:
  enum class VIDEO_QUALITY { LOW = 0, MEDIUM = 1, HIGH = 2 };
  enum class LANGUAGE { CHINESE = 0, ENGLISH = 1 };

 public:
  ConfigCenter();
  ~ConfigCenter();

 public:
  int SetVideoQuality(VIDEO_QUALITY video_quality);
  int SetLanguage(LANGUAGE language);

 public:
  VIDEO_QUALITY GetVideoQuality();
  LANGUAGE GetLanguage();

 private:
  VIDEO_QUALITY video_quality_ = VIDEO_QUALITY::MEDIUM;
  LANGUAGE language_ = LANGUAGE::ENGLISH;
};

#endif