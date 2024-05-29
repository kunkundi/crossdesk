#include "config_center.h"

ConfigCenter::ConfigCenter() {}

ConfigCenter::~ConfigCenter() {}

int ConfigCenter::SetVideoQuality(VIDEO_QUALITY video_quality) {
  video_quality_ = video_quality;
  return 0;
}

int ConfigCenter::SetLanguage(LANGUAGE language) {
  language_ = language;
  return 0;
}

ConfigCenter::VIDEO_QUALITY ConfigCenter::GetVideoQuality() {
  return video_quality_;
}

ConfigCenter::LANGUAGE ConfigCenter::GetLanguage() { return language_; }