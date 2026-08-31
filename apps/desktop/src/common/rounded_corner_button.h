/*
 * @Author: DI JUNKUN
 * @Date: 2026-02-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ROUNDED_CORNER_BUTTON_H_
#define _ROUNDED_CORNER_BUTTON_H_

#include "imgui.h"
#include "imgui_internal.h"

namespace crossdesk {
bool RoundedCornerButton(const char* label, const ImVec2& size, float rounding,
                         ImDrawFlags round_flags, bool enabled = true,
                         ImU32 normal_col = 0, ImU32 hover_col = 0,
                         ImU32 active_col = 0, ImU32 border_col = 0);
}  // namespace crossdesk

#endif