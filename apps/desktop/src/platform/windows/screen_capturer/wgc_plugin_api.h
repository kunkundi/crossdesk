/*
 * @Author: DI JUNKUN
 * @Date: 2026-03-20
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _WGC_PLUGIN_API_H_
#define _WGC_PLUGIN_API_H_

#include "screen_capturer.h"

namespace crossdesk {
class ScreenCapturer;
}

#if defined(_WIN32) && defined(CROSSDESK_WGC_PLUGIN_BUILD)
#define CROSSDESK_WGC_PLUGIN_API __declspec(dllexport)
#else
#define CROSSDESK_WGC_PLUGIN_API
#endif

extern "C" {
CROSSDESK_WGC_PLUGIN_API crossdesk::ScreenCapturer*
CrossDeskCreateWgcCapturer();
CROSSDESK_WGC_PLUGIN_API void CrossDeskDestroyWgcCapturer(
    crossdesk::ScreenCapturer* capturer);
}

#endif