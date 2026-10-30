#include "terminal_renderer.h"
#include <charconv>
#include <cmath>

namespace crossdesk::terminal {
void TerminalMetrics::BeginQuery() {
  queried_ = true;
  direct_reply_ = false;
  pixel_width_ = pixel_height_ = columns_ = rows_ = 0;
}

bool TerminalMetrics::SetCellSize(double width, double height) {
  if (width < 1 || height < 1 || width > 1024 || height > 1024 ||
      width / height < .2 || width / height > 2)
    return false;
  cell_width_ = width;
  cell_height_ = height;
  return true;
}

void TerminalMetrics::Report(const std::string &sequence) {
  if (!queried_ || sequence.size() < 9 || sequence.compare(0, 2, "\033[") ||
      sequence.back() != 't')
    return;
  int type = 0, height = 0, width = 0;
  const char *cur = sequence.data() + 2;
  const char *end = sequence.data() + sequence.size() - 1;
  for (int *value : {&type, &height, &width}) {
    auto result = std::from_chars(cur, end, *value);
    if (result.ec != std::errc{} || *value <= 0 || *value > 1048576)
      return;
    cur = result.ptr;
    if (value != &width) {
      if (cur == end || *cur++ != ';')
        return;
    } else if (cur != end) {
      return;
    }
  }
  if (type == 6) {
    // CSI 16 t replies with character height, then width (not width/height).
    if (SetCellSize(width, height))
      direct_reply_ = true;
  } else if (type == 4) {
    pixel_width_ = width;
    pixel_height_ = height;
  } else if (type == 8) {
    columns_ = width;
    rows_ = height;
  } else {
    return;
  }
  // VTE supports text-area pixels/cells even when CSI 16 t is unavailable.
  // Use reported full rows/columns, not the capped video viewport or status
  // rows, and retain fractional cell dimensions for scaled terminal fonts.
  if (!direct_reply_ && pixel_width_ && pixel_height_ && columns_ && rows_)
    SetCellSize(double(pixel_width_) / columns_, double(pixel_height_) / rows_);
}

void TerminalMetrics::Apply(Viewport &viewport) const {
  if (!Known())
    return;
  viewport.cell_aspect = cell_width_ / cell_height_;
  viewport.pixel_width = int(std::lround(cell_width_ * viewport.columns));
  viewport.pixel_height = int(std::lround(cell_height_ * viewport.rows));
}
} // namespace crossdesk::terminal
