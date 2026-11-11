/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_RESOLUTION_LINUX_H_
#define _DISPLAY_RESOLUTION_LINUX_H_

#include <functional>
#include <vector>

#include <display_resolution.h>
#include "display_info.h"

namespace crossdesk {

// Native X11/RandR outputs only. Wayland/Xwayland cannot change the physical
// desktop through RandR and must not advertise these modes to controllers.
std::vector<DisplayResolutionInfo> QueryLinuxDisplayResolutions(
    const std::vector<DisplayInfo>& displays);
bool ApplyLinuxDisplayResolution(const std::vector<DisplayInfo>& displays,
                                 const DisplayResolutionChange& change);
std::function<bool()> CaptureLinuxDisplayResolution(
    const std::vector<DisplayInfo>& displays,
    const DisplayResolutionChange& change);

}  // namespace crossdesk

#endif