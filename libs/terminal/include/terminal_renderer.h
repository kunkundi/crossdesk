/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-30
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _TERMINAL_RENDERER_H_
#define _TERMINAL_RENDERER_H_

#include <controller_session.h>
namespace crossdesk::terminal {
enum class AnsiDetail { Half, Quadrant };
struct Viewport {
  int columns = 80, rows = 23;
  double cell_aspect = .5;
  bool utf8 = true;
  int colors = 24;
  AnsiDetail detail = AnsiDetail::Half;
  double zoom = 1, center_x = .5, center_y = .5;
  int pixel_width = 0, pixel_height = 0;
  bool native = false, pixel_mode = false;
};
struct Rect {
  int x, y, width, height;
};
struct Cell {
  uint32_t top = 0, bottom = 0;
  uint32_t glyph = 0x2580;
  bool operator==(const Cell &other) const {
    return top == other.top && bottom == other.bottom && glyph == other.glyph;
  }
};
struct Grid {
  Viewport viewport;
  Rect content{};
  std::vector<Cell> cells;
  Rect source{};
  unsigned source_width = 0, source_height = 0;
  int samples_x = 1, samples_y = 2;
};
Rect Fit(int width, int height, int columns, int pixel_rows,
         double pixel_aspect = 1);
Grid Layout(const DecodedFrame &, Viewport);
Grid Convert(const DecodedFrame &, Viewport);
std::optional<std::pair<float, float>> MapPointer(const Grid &, int column,
                                                  int row);
std::vector<std::string> Lines(const Grid &);
std::string Difference(const Grid &, const std::optional<Grid> &previous);
std::string Kitty(const DecodedFrame &, const Viewport &);
inline constexpr char kKittyQuery[] =
    "\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\";
inline constexpr char kKittyDelete[] = "\033_Ga=d,d=I,i=31,q=2\033\\";
bool KittyReply(const std::string &response);
// Incremental parser: protocol replies and unknown escapes are consumed
// locally. Key taps always release immediately; paste uses the UTF-8 text
// protocol. In command_mode, control records the mode to restore while the
// session's remote input is suspended until the user explicitly leaves the mode.
class Input {
public:
  void Feed(const std::string &bytes, ControllerSession &, const Grid *grid);
  bool exit = false, control = false, native = false, command_mode = false;
  std::string error;
  double zoom = 1, center_x = .5, center_y = .5;
  uint64_t view_revision = 0;
  uint64_t mouse_reports = 0;

private:
  std::string pending_;
  std::chrono::steady_clock::time_point last_input_{};
  bool pasting_ = false;
  float pointer_x_ = .5f, pointer_y_ = .5f;
  void Tap(ControllerSession &, int key, int modifier = 0);
  bool LocalKey(int key, ControllerSession &, const Grid *);
  void Navigate(char key, const Grid *);
};
} // namespace crossdesk::terminal

#endif
