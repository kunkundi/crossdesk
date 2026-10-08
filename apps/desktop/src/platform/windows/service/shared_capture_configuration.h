/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SHARED_CAPTURE_CONFIGURATION_H_
#define _SHARED_CAPTURE_CONFIGURATION_H_

namespace crossdesk {

// Desktop stage/name are hints for initial binding, not image configuration.
// The producer follows the actual input desktop without restarting the stream.
struct SharedCaptureConfiguration {
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
  bool show_cursor = true;
  int fps = 30;

  bool operator==(const SharedCaptureConfiguration& other) const {
    return left == other.left && top == other.top && width == other.width &&
           height == other.height && show_cursor == other.show_cursor &&
           fps == other.fps;
  }
};

}  // namespace crossdesk
#endif  // _SHARED_CAPTURE_CONFIGURATION_H_
