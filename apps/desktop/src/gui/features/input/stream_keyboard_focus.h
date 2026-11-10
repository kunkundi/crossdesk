/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _STREAM_KEYBOARD_FOCUS_H_
#define _STREAM_KEYBOARD_FOCUS_H_

namespace crossdesk {

// Native window focus and ownership of keyboard input are different: a local
// menu keeps the window active while temporarily suspending remote input.
// Publish the new state before releasing input, since UI callbacks can reenter.
template <typename Release>
void UpdateStreamKeyboardFocus(bool focused, bool local_input_active,
                               bool& remote_focused, Release release) {
  focused = focused && !local_input_active;
  if (remote_focused == focused) return;
  remote_focused = focused;
  if (!focused) release(local_input_active);
}

}  // namespace crossdesk

#endif