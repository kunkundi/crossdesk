/*
 * @Author: DI JUNKUN
 * @Date: 2025-03-06
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RESOLUTION_ADAPTER_H_
#define _RESOLUTION_ADAPTER_H_

#include <vector>

#include "resolution_bitrate_limits.h"
#include "x.h"

class ResolutionAdapter {
 public:
  ResolutionAdapter() = default;
  ~ResolutionAdapter() = default;

 public:
  int GetResolution(int target_bitrate, int current_width, int current_height,
                    int* target_width, int* target_height);

  int ResolutionDowngrade(const XVideoFrame* video_frame, int target_width,
                          int target_height, XVideoFrame* new_frame);

 public:
  std::vector<ResolutionBitrateLimits> GetBitrateLimits() {
    return {{320, 180, 0, 30000, 300000},
            {480, 270, 300000, 30000, 500000},
            {640, 360, 500000, 30000, 800000},
            {960, 540, 800000, 30000, 1500000},
            {1280, 720, 1500000, 30000, 2500000},
            {1920, 1080, 2500000, 30000, 4000000}};
  }

  int SetTargetBitrate(int bitrate);
};

#endif