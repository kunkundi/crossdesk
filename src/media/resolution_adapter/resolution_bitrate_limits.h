/*
 * @Author: DI JUNKUN
 * @Date: 2025-03-07
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RESOLUTION_BITRATE_LIMITS_H_
#define _RESOLUTION_BITRATE_LIMITS_H_

struct ResolutionBitrateLimits {
  ResolutionBitrateLimits(int width, int height, int min_start_bitrate_bps,
                          int min_bitrate_bps, int max_bitrate_bps)
      : width(width),
        height(height),
        min_start_bitrate_bps(min_start_bitrate_bps),
        min_bitrate_bps(min_bitrate_bps),
        max_bitrate_bps(max_bitrate_bps) {}
  int width = 0;
  int height = 0;
  int min_start_bitrate_bps = 0;
  int min_bitrate_bps = 0;
  int max_bitrate_bps = 0;
  bool operator==(const ResolutionBitrateLimits& rhs) const;
  bool operator!=(const ResolutionBitrateLimits& rhs) const {
    return !(*this == rhs);
  }
};

#endif