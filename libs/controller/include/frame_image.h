/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-02
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FRAME_IMAGE_H_
#define _FRAME_IMAGE_H_

#include <controller_session.h>

namespace crossdesk {
struct ImageRegion {
  int x = 0, y = 0, width = 0, height = 0;
};
struct FrameImage {
  int width = 0, height = 0;
  ImageRegion region;
  std::vector<uint8_t> png;
};
// Pure bounded conversion; safe on an encoding worker, never calls the session.
FrameImage EncodeFrameImage(const DecodedFrame &frame, ImageRegion region,
                            int max_edge, size_t max_bytes);
std::string ImageBase64(const std::vector<uint8_t> &bytes);
} // namespace crossdesk

#endif
