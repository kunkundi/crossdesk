/*
 * Aggregate state visible to GuiRuntime and GuiApplication.
 *
 * Definitions live beside their owners: SDL/window state in application and
 * connection/session state in runtime. This header is intentionally only the
 * composition point.
 */

#ifndef CROSSDESK_GUI_STATE_H_
#define CROSSDESK_GUI_STATE_H_

#include "application/application_state.h"
#include "runtime/runtime_state.h"

namespace crossdesk::gui_detail {

struct GuiState : ApplicationState, RuntimeState {};

} // namespace crossdesk::gui_detail

#endif // CROSSDESK_GUI_STATE_H_
