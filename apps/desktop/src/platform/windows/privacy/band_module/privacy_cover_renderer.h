/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PRIVACY_COVER_RENDERER_H_
#define _PRIVACY_COVER_RENDERER_H_

#include "../privacy_band_ipc.h"

namespace crossdesk {

// Paints the local, opaque cover. The caller owns the HWND and its DPI context.
bool DrawPrivacyCover(HDC dc, const RECT& rect, UINT dpi, HINSTANCE module,
                      const wchar_t* unlock_hint, PrivacyCoverStyle style);

}  // namespace crossdesk

#endif  // _PRIVACY_COVER_RENDERER_H_
