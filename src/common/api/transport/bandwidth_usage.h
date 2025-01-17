/*
 * @Author: DI JUNKUN
 * @Date: 2025-01-15
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _BANDWIDTH_USAGE_H_
#define _BANDWIDTH_USAGE_H_

enum class BandwidthUsage {
  kBwNormal = 0,
  kBwUnderusing = 1,
  kBwOverusing = 2,
  kLast
};

#endif