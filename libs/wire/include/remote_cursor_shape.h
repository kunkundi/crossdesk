/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-01
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _REMOTE_CURSOR_SHAPE_H_
#define _REMOTE_CURSOR_SHAPE_H_

#include <cstdint>

namespace crossdesk {

// Keep these values aligned with Slint's MouseCursor enum. The wire
// intentionally carries a semantic cursor instead of a platform handle so a
// Windows, macOS or Linux host can control a different desktop platform.
enum class RemoteCursorShape : uint8_t {
  default_cursor = 0,
  none,
  help,
  pointer,
  progress,
  wait,
  crosshair,
  text,
  alias,
  copy,
  move,
  no_drop,
  not_allowed,
  grab,
  grabbing,
  col_resize,
  row_resize,
  n_resize,
  e_resize,
  s_resize,
  w_resize,
  ne_resize,
  nw_resize,
  se_resize,
  sw_resize,
  ew_resize,
  ns_resize,
  nesw_resize,
  nwse_resize,
};

}  // namespace crossdesk

#endif
