/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _GUI_STATE_H_
#define _GUI_STATE_H_

#include "application/application_state.h"
#include "runtime/runtime_state.h"

namespace crossdesk::gui_detail {

struct GuiState : ApplicationState, RuntimeState {};

} // namespace crossdesk::gui_detail

#endif