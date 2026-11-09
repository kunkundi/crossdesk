/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURE_METHOD_H_
#define _SCREEN_CAPTURE_METHOD_H_

namespace crossdesk {

// Backend identifiers. Auto starts the fixed DXGI/WGC/GDI selection order.
enum class ScreenCaptureMethod { Auto = 0, Dxgi = 1, Wgc = 2, Gdi = 3 };

}  // namespace crossdesk

#endif
