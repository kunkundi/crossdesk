#include "terminal_renderer.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <zlib.h>
namespace crossdesk::terminal {
Rect Fit(int w, int h, int cols, int rows, double aspect) {
  if (w <= 0 || h <= 0 || cols <= 0 || rows <= 0 || !std::isfinite(aspect) ||
      aspect <= 0)
    return {};
  const double scale = std::min(cols * aspect / w, double(rows) / h);
  const int cw = std::max(1, std::min(cols, int(w * scale / aspect)));
  const int ch = std::max(1, std::min(rows, int(h * scale)));
  return {(cols - cw) / 2, (rows - ch) / 2, cw, ch};
}
static uint32_t Pixel(const DecodedFrame &f, int x, int y) {
  const size_t offset =
      size_t(f.width) * f.height + (y / 2) * f.width + (x & ~1);
  int yy = std::max(0, int(f.nv12[size_t(y) * f.width + x]) - 16);
  int u = int(f.nv12[offset]) - 128, v = int(f.nv12[offset + 1]) - 128;
  auto clamp = [](int n) { return uint32_t(std::clamp(n, 0, 255)); };
  return (clamp((298 * yy + 409 * v + 128) >> 8) << 16) |
         (clamp((298 * yy - 100 * u - 208 * v + 128) >> 8) << 8) |
         clamp((298 * yy + 516 * u + 128) >> 8);
}
static void Validate(const DecodedFrame &f, const Viewport &v) {
  if (v.columns < 1 || v.rows < 1 || v.columns > 240 || v.rows > 100 ||
      v.row_offset < 0 || v.row_offset > 100 ||
      !std::isfinite(v.cell_aspect) || v.cell_aspect < .2 ||
      v.cell_aspect > 2 || !std::isfinite(v.zoom) || v.zoom < 1 ||
      v.zoom > 32 || !std::isfinite(v.center_x) || !std::isfinite(v.center_y) ||
      v.center_x < 0 || v.center_x > 1 || v.center_y < 0 || v.center_y > 1 ||
      !f.width || !f.height || f.width > 8192 || f.height > 8192 ||
      (f.width & 1) || (f.height & 1) ||
      size_t(f.width) * f.height > 16777216 ||
      f.nv12.size() < size_t(f.width) * f.height * 3 / 2)
    throw std::invalid_argument("invalid frame or viewport");
}
static Rect Crop(const DecodedFrame &f, const Viewport &v) {
  double width = f.width, height = f.height;
  if (v.native) {
    const int sx = v.utf8 && v.detail == AnsiDetail::Quadrant ? 2 : 1;
    const int sy = v.utf8 ? 2 : 1;
    const int ow = v.pixel_mode ? std::clamp(v.pixel_width > 0 ? v.pixel_width
                                                               : v.columns * 10,
                                             2, 1920)
                                : v.columns * sx;
    const int oh = v.pixel_mode ? std::clamp(v.pixel_height > 0 ? v.pixel_height
                                                                : v.rows * 20,
                                             2, 1080)
                                : v.rows * sy;
    const double aspect =
        double(v.columns) * v.cell_aspect * oh / (v.rows * ow);
    // Fit square source pixels into non-square character samples without
    // downsampling either dimension. Native mode may show a small crop.
    width = std::min(width, std::floor(ow * std::min(1.0, aspect)));
    height = std::min(height, std::floor(oh * std::min(1.0, 1 / aspect)));
  }
  int w = std::max(1, int(width / v.zoom)),
      h = std::max(1, int(height / v.zoom));
  int x = std::clamp(int(v.center_x * f.width - w * .5), 0, int(f.width) - w);
  int y = std::clamp(int(v.center_y * f.height - h * .5), 0, int(f.height) - h);
  return {x, y, w, h};
}
// Area resampling integrates the entire source footprint. Thin lines cannot
// disappear merely because a nearest-neighbour sample landed beside them.
static uint32_t Sample(const DecodedFrame &f, double x0, double y0, double x1,
                       double y1) {
  double sums[3] = {}, weight = 0;
  for (int y = int(std::floor(y0));
       y < std::min(int(f.height), int(std::ceil(y1))); ++y) {
    double wy = std::min(y1, double(y + 1)) - std::max(y0, double(y));
    for (int x = int(std::floor(x0));
         x < std::min(int(f.width), int(std::ceil(x1))); ++x) {
      double w = wy * (std::min(x1, double(x + 1)) - std::max(x0, double(x)));
      auto c = Pixel(f, std::max(0, x), std::max(0, y));
      sums[0] += (c >> 16) * w;
      sums[1] += ((c >> 8) & 255) * w;
      sums[2] += (c & 255) * w;
      weight += w;
    }
  }
  if (weight <= 0)
    return 0;
  auto round = [&](int i) {
    return uint32_t(std::clamp(int(std::lround(sums[i] / weight)), 0, 255));
  };
  return (round(0) << 16) | (round(1) << 8) | round(2);
}
using QuadrantPixels = std::array<uint32_t, 4>;
struct QuadrantFit {
  Cell cell;
  QuadrantPixels pixels{};
  double error = 0;
};
struct QuadrantEdge {
  double weight = 0;
  std::array<double, 3> error{};
};
using QuadrantEdges = std::array<QuadrantEdge, 4>;
static int Channel(uint32_t color, int channel) {
  return (color >> (16 - 8 * channel)) & 255;
}
// Seven partitions suffice; their complements produce the same pixel colours.
static constexpr uint32_t kQuadrantGlyphs[] = {0x20,   0x2598, 0x259D, 0x2580,
                                               0x2596, 0x258C, 0x259E, 0x259B};
static QuadrantFit Quadrant(const QuadrantPixels &pixels) {
  int channels[4][3], total[3] = {}, squares = 0;
  uint32_t mean = 0;
  bool flat = true;
  for (int c = 0; c < 3; ++c) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 4; ++i) {
      int value = channels[i][c] = Channel(pixels[i], c);
      lo = std::min(lo, value);
      hi = std::max(hi, value);
      total[c] += value;
      squares += value * value;
    }
    // Keep the per-channel threshold small to retain faint/coloured detail
    // above this range, rather than judging flatness by average luminance.
    flat = flat && hi - lo <= 6;
    mean |= uint32_t((total[c] + 2) / 4) << (16 - 8 * c);
  }
  if (flat) {
    QuadrantFit fit{
        {mean, mean, 0x20}, {mean, mean, mean, mean}, double(squares)};
    for (int c = 0; c < 3; ++c) {
      int value = Channel(mean, c);
      fit.error += 4 * value * value - 2 * value * total[c];
    }
    return fit;
  }
  // Reuse subset sums. Integer rounded means and sum-of-squares give exactly
  // the original RGB squared error without rescanning four pixels per mask.
  static constexpr int counts[] = {0, 1, 1, 2, 1, 2, 2, 3};
  int sums[8][3] = {};
  QuadrantFit best;
  int best_error = INT32_MAX, best_mask = 0;
  for (int mask = 1; mask < 8; ++mask) {
    int bit = mask & 1 ? 0 : mask & 2 ? 1 : 2;
    int rest = mask & (mask - 1), count = counts[mask];
    uint32_t fg = 0, bg = 0;
    int error = squares;
    for (int c = 0; c < 3; ++c) {
      int sum = sums[mask][c] = sums[rest][c] + channels[bit][c];
      int front = (sum + count / 2) / count;
      int back = (total[c] - sum + (4 - count) / 2) / (4 - count);
      fg |= uint32_t(front) << (16 - 8 * c);
      bg |= uint32_t(back) << (16 - 8 * c);
      error += count * front * front - 2 * front * sum +
               (4 - count) * back * back - 2 * back * (total[c] - sum);
    }
    if (error < best_error) {
      best_error = error;
      best_mask = mask;
      best.cell = {fg, bg, kQuadrantGlyphs[mask]};
    }
  }
  best.error = best_error;
  for (int i = 0; i < 4; ++i)
    best.pixels[i] = (best_mask >> i) & 1 ? best.cell.top : best.cell.bottom;
  if (best.cell.top == best.cell.bottom)
    best.cell.glyph = 0x20;
  return best;
}
static QuadrantFit RefineQuadrant(const QuadrantPixels &pixels,
                                  const QuadrantEdges &edges,
                                  const QuadrantFit &initial) {
  int channels[4][3];
  double weighted[4][3];
  for (int i = 0; i < 4; ++i)
    for (int c = 0; c < 3; ++c) {
      channels[i][c] = Channel(pixels[i], c);
      weighted[i][c] =
          (1 + edges[i].weight) * channels[i][c] + edges[i].error[c];
    }
  auto score = [&](const QuadrantFit &fit) {
    double value = fit.error;
    for (int i = 0; i < 4; ++i)
      for (int c = 0; c < 3; ++c) {
        double d = Channel(fit.pixels[i], c) - channels[i][c];
        // Match neighbouring quantization errors, preserving the source
        // gradient. The neighbour-only constant does not affect selection.
        value += edges[i].weight * d * d - 2 * edges[i].error[c] * d;
      }
    return value;
  };
  QuadrantFit best = initial;
  double best_score = score(best);
  const double max_error = initial.error + std::min(48.0, initial.error * .05);
  for (unsigned mask = 1; mask < 8; ++mask) {
    double sums[2][3] = {};
    double weights[2] = {};
    for (int i = 0; i < 4; ++i) {
      int g = (mask >> i) & 1;
      double weight = 1 + edges[i].weight;
      weights[g] += weight;
      for (int c = 0; c < 3; ++c)
        sums[g][c] += weighted[i][c];
    }
    uint32_t colors[2] = {};
    for (int g = 0; g < 2; ++g)
      for (int c = 0; c < 3; ++c) {
        sums[g][c] =
            std::clamp(std::round(sums[g][c] / weights[g]), 0.0, 255.0);
        colors[g] |= uint32_t(sums[g][c]) << (16 - 8 * c);
      }
    QuadrantFit fit{{colors[1], colors[0], kQuadrantGlyphs[mask]}};
    for (int i = 0; i < 4; ++i) {
      fit.pixels[i] = colors[(mask >> i) & 1];
      for (int c = 0; c < 3; ++c) {
        double d = channels[i][c] - Channel(fit.pixels[i], c);
        fit.error += d * d;
      }
    }
    // Limit the cost of continuity to 5% of the independent fit's error,
    // capped at 2 RGB levels RMS over the cell's twelve channel samples.
    if (fit.error > max_error)
      continue;
    double value = score(fit);
    if (value < best_score) {
      best_score = value;
      best = fit;
    }
  }
  if (best.cell.top == best.cell.bottom)
    best.cell.glyph = 0x20;
  return best;
}
static void AppendUtf8(std::string &out, uint32_t c) {
  if (c < 128) {
    out += char(c);
  } else if (c < 2048) {
    out += char(0xc0 | (c >> 6));
    out += char(0x80 | (c & 63));
  } else {
    out += char(0xe0 | (c >> 12));
    out += char(0x80 | ((c >> 6) & 63));
    out += char(0x80 | (c & 63));
  }
}
static void AppendNumber(std::string &out, uint32_t value) {
  char digits[10];
  auto result = std::to_chars(digits, digits + sizeof(digits), value);
  out.append(digits, result.ptr);
}
Grid Layout(const DecodedFrame &f, Viewport v) {
  Validate(f, v);
  const int sx = v.utf8 && v.detail == AnsiDetail::Quadrant ? 2 : 1,
            sy = v.utf8 ? 2 : 1;
  const int width = v.columns * sx, height = v.rows * sy;
  Rect crop = Crop(f, v);
  Grid g{v,
         Fit(crop.width, crop.height, width, height, v.cell_aspect * sy / sx),
         {}};
  g.source = crop;
  g.source_width = f.width;
  g.source_height = f.height;
  g.samples_x = sx;
  g.samples_y = sy;
  return g;
}
Grid Convert(const DecodedFrame &f, Viewport v) {
  auto g = Layout(f, v);
  const auto crop = g.source;
  const int sx = g.samples_x, sy = g.samples_y;
  g.cells.resize(size_t(v.columns) * v.rows);
  std::vector<QuadrantPixels> samples(sx == 2 ? g.cells.size() : 0);
  std::vector<QuadrantFit> fits(samples.size());
  auto pixel = [&](int x, int y) {
    const auto &r = g.content;
    if (x < r.x || y < r.y || x >= r.x + r.width || y >= r.y + r.height)
      return uint32_t(0);
    double x0 = crop.x + double(x - r.x) * crop.width / r.width;
    double y0 = crop.y + double(y - r.y) * crop.height / r.height;
    return Sample(f, x0, y0, x0 + double(crop.width) / r.width,
                  y0 + double(crop.height) / r.height);
  };
  for (int y = 0; y < v.rows; ++y)
    for (int x = 0; x < v.columns; ++x) {
      const auto index = size_t(y) * v.columns + x;
      auto &c = g.cells[index];
      if (sx == 2) {
        samples[index] = {pixel(x * 2, y * 2), pixel(x * 2 + 1, y * 2),
                          pixel(x * 2, y * 2 + 1), pixel(x * 2 + 1, y * 2 + 1)};
        fits[index] = Quadrant(samples[index]);
        c = fits[index].cell;
      } else {
        c.top = pixel(x, y * sy);
        c.bottom = sy == 1 ? c.top : pixel(x, y * sy + sy - 1);
      }
    }
  // Read only independent fits so refinement has no left-to-right/top-to-bottom
  // feedback, and a changed cell cannot propagate through the entire frame.
  if (sx == 2)
    for (int y = 0; y < v.rows; ++y)
      for (int x = 0; x < v.columns; ++x) {
        const auto index = size_t(y) * v.columns + x;
        if (fits[index].cell.glyph == 0x20 || fits[index].error == 0)
          continue;
        QuadrantEdges edges{};
        auto edge = [&](int i, size_t neighbour, int j) {
          int contrast = 0;
          for (int c = 0; c < 3; ++c)
            contrast =
                std::max(contrast, std::abs(Channel(samples[index][i], c) -
                                            Channel(samples[neighbour][j], c)));
          // Fade out across real edges instead of smoothing text into its
          // background. Equal source colours receive the strongest constraint.
          double strength = std::max(0.0, 1 - contrast / 24.0);
          double weight = .35 * strength * strength;
          edges[i].weight += weight;
          for (int c = 0; c < 3; ++c)
            edges[i].error[c] +=
                weight * (Channel(fits[neighbour].pixels[j], c) -
                          Channel(samples[neighbour][j], c));
        };
        if (x > 0) {
          edge(0, index - 1, 1);
          edge(2, index - 1, 3);
        }
        if (x + 1 < v.columns) {
          edge(1, index + 1, 0);
          edge(3, index + 1, 2);
        }
        if (y > 0) {
          edge(0, index - v.columns, 2);
          edge(1, index - v.columns, 3);
        }
        if (y + 1 < v.rows) {
          edge(2, index + v.columns, 0);
          edge(3, index + v.columns, 1);
        }
        if (edges[0].weight + edges[1].weight + edges[2].weight +
                edges[3].weight >
            0)
          g.cells[index] =
              RefineQuadrant(samples[index], edges, fits[index]).cell;
      }
  return g;
}
std::optional<std::pair<float, float>> MapPointer(const Grid &g, int x, int y) {
  x *= g.samples_x;
  y -= g.viewport.row_offset;
  y *= g.samples_y;
  const auto &r = g.content;
  if (!r.width || !r.height || !g.source_width || !g.source_height || x < r.x ||
      y < r.y || x >= r.x + r.width || y >= r.y + r.height)
    return {};
  float fx = float(x - r.x) / std::max(1, r.width - 1),
        fy = float(y - r.y) / std::max(1, r.height - 1);
  return std::pair<float, float>{
      (g.source.x + fx * std::max(0, g.source.width - 1)) /
          std::max(1u, g.source_width - 1),
      (g.source.y + fy * std::max(0, g.source.height - 1)) /
          std::max(1u, g.source_height - 1)};
}
static uint32_t TerminalColor(uint32_t c, int depth) {
  const int r = c >> 16, g = (c >> 8) & 255, b = c & 255;
  if (depth == 24)
    return c;
  if (depth == 256) {
    constexpr int levels[] = {0, 95, 135, 175, 215, 255};
    auto index = [&](int value) {
      int best = 0;
      for (int i = 1; i < 6; ++i)
        if (std::abs(levels[i] - value) < std::abs(levels[best] - value))
          best = i;
      return best;
    };
    int ri = index(r), gi = index(g), bi = index(b);
    int cube = (r - levels[ri]) * (r - levels[ri]) +
               (g - levels[gi]) * (g - levels[gi]) +
               (b - levels[bi]) * (b - levels[bi]);
    int gray =
            std::clamp(int(std::lround(((r + g + b) / 3.0 - 8) / 10)), 0, 23),
        gv = 8 + 10 * gray;
    int gray_error =
        (r - gv) * (r - gv) + (g - gv) * (g - gv) + (b - gv) * (b - gv);
    return gray_error < cube ? 232 + gray : 16 + 36 * ri + 6 * gi + bi;
  }
  return (r >= 128 ? 1 : 0) + (g >= 128 ? 2 : 0) + (b >= 128 ? 4 : 0);
}
static void AppendColor(std::string &out, uint32_t c, bool fg, int depth) {
  if (depth == 24 || depth == 256) {
    out += fg ? "\033[38;" : "\033[48;";
    if (depth == 24) {
      out += "2;";
      AppendNumber(out, c >> 16);
      out += ';';
      AppendNumber(out, (c >> 8) & 255);
      out += ';';
      AppendNumber(out, c & 255);
    } else {
      out += "5;";
      AppendNumber(out, c);
    }
  } else {
    out += "\033[";
    AppendNumber(out, (fg ? 30 : 40) + c);
  }
  out += 'm';
}
struct AnsiColors {
  uint32_t fg = UINT32_MAX, bg = UINT32_MAX;
};
static void AppendCells(std::string &out, const Grid &g, int y, int begin,
                        int end, AnsiColors &colors) {
  for (int x = begin; x < end; ++x) {
    const auto &c = g.cells[size_t(y) * g.viewport.columns + x];
    // Spaces need no foreground SGR. Keep backgrounds for block glyphs too,
    // since font rasterization can expose them at the cell's edges.
    if (g.viewport.utf8 && c.glyph != 0x20) {
      auto fg = TerminalColor(c.top, g.viewport.colors);
      if (colors.fg != fg) {
        AppendColor(out, fg, true, g.viewport.colors);
        colors.fg = fg;
      }
    }
    // Track the emitted palette index so quantized colours do not resend SGR.
    auto bg =
        TerminalColor(g.viewport.utf8 ? c.bottom : c.top, g.viewport.colors);
    if (colors.bg != bg) {
      AppendColor(out, bg, false, g.viewport.colors);
      colors.bg = bg;
    }
    AppendUtf8(out, g.viewport.utf8 ? c.glyph : 0x20);
  }
}
std::vector<std::string> Lines(const Grid &g) {
  std::vector<std::string> lines;
  lines.reserve(g.viewport.rows);
  for (int y = 0; y < g.viewport.rows; ++y) {
    std::string line;
    line.reserve(size_t(g.viewport.columns) * 40 + 4);
    AnsiColors colors;
    AppendCells(line, g, y, 0, g.viewport.columns, colors);
    line += "\033[0m";
    lines.push_back(std::move(line));
  }
  return lines;
}
std::string Difference(const Grid &g, const std::optional<Grid> &previous) {
  std::string out;
  AnsiColors colors;
  const bool same = previous &&
                    previous->viewport.columns == g.viewport.columns &&
                    previous->viewport.rows == g.viewport.rows &&
                    previous->viewport.row_offset == g.viewport.row_offset &&
                    previous->viewport.colors == g.viewport.colors &&
                    previous->viewport.utf8 == g.viewport.utf8 &&
                    previous->cells.size() == g.cells.size();
  for (int y = 0; y < g.viewport.rows; ++y) {
    int begin = 0, end = g.viewport.columns;
    if (same) {
      const auto offset = size_t(y) * g.viewport.columns;
      while (begin < end &&
             g.cells[offset + begin] == previous->cells[offset + begin])
        ++begin;
      while (end > begin &&
             g.cells[offset + end - 1] == previous->cells[offset + end - 1])
        --end;
    }
    if (begin == end)
      continue;
    out += "\033[";
    AppendNumber(out, g.viewport.row_offset + y + 1);
    out += ';';
    AppendNumber(out, begin + 1);
    out += 'H';
    AppendCells(out, g, y, begin, end, colors);
  }
  if (!out.empty())
    out += "\033[0m";
  return out;
}
static std::string Base64(const std::vector<uint8_t> &data) {
  constexpr char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    unsigned n = unsigned(data[i]) << 16;
    if (i + 1 < data.size())
      n |= unsigned(data[i + 1]) << 8;
    if (i + 2 < data.size())
      n |= data[i + 2];
    out += table[n >> 18];
    out += table[(n >> 12) & 63];
    out += i + 1 < data.size() ? table[(n >> 6) & 63] : '=';
    out += i + 2 < data.size() ? table[n & 63] : '=';
  }
  return out;
}
PixelFrame KittyPixels(const DecodedFrame &f, const Viewport &requested) {
  auto v = requested;
  v.pixel_mode = true;
  Validate(f, v);
  PixelFrame pixels;
  pixels.width =
      std::clamp(v.pixel_width > 0 ? v.pixel_width : v.columns * 10, 2, 1920);
  pixels.height =
      std::clamp(v.pixel_height > 0 ? v.pixel_height : v.rows * 20, 2, 1080);
  pixels.rgb.resize(size_t(pixels.width) * pixels.height * 3, 0);
  const auto crop = Crop(f, v);
  const auto r = Fit(crop.width, crop.height, pixels.width, pixels.height,
                     double(v.columns) * v.cell_aspect * pixels.height /
                         (v.rows * pixels.width));
  if (r.width == crop.width && r.height == crop.height) {
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        const auto c = Pixel(f, crop.x + x, crop.y + y);
        auto *dst = &pixels.rgb[(size_t(y + r.y) * pixels.width + x + r.x) * 3];
        dst[0] = c >> 16;
        dst[1] = c >> 8;
        dst[2] = c;
      }
    return pixels;
  }
  // Separable area integration. Integer overlap weights avoid per-pixel
  // floor/ceil and repeated NV12 conversion in overlapping vertical samples.
  // Adjacent output rows can share only their boundary source row, so one
  // cached horizontal row suffices, even when upscaling.
  struct Span {
    int first, last, begin, end;
  };
  std::vector<Span> spans;
  for (int x = 0; x < r.width; ++x) {
    const int begin = x * crop.width, end = begin + crop.width;
    spans.push_back(
        {begin / r.width, (end + r.width - 1) / r.width, begin, end});
  }
  std::vector<uint32_t> source(crop.width), horizontal(size_t(r.width) * 3);
  std::vector<uint64_t> sums(size_t(r.width) * 3);
  const uint64_t area = uint64_t(crop.width) * crop.height;
  int cached_y = -1;
  for (int y = 0; y < r.height; ++y) {
    std::fill(sums.begin(), sums.end(), 0);
    const int begin = y * crop.height, end = begin + crop.height;
    for (int sy = begin / r.height; sy < (end + r.height - 1) / r.height;
         ++sy) {
      if (cached_y != sy) {
        for (int x = 0; x < crop.width; ++x)
          source[x] = Pixel(f, crop.x + x, crop.y + sy);
        std::fill(horizontal.begin(), horizontal.end(), 0);
        for (int x = 0; x < r.width; ++x) {
          const auto &span = spans[x];
          for (int sx = span.first; sx < span.last; ++sx) {
            const unsigned weight = std::min(span.end, (sx + 1) * r.width) -
                                    std::max(span.begin, sx * r.width);
            const auto c = source[sx];
            horizontal[x * 3] += (c >> 16) * weight;
            horizontal[x * 3 + 1] += ((c >> 8) & 255) * weight;
            horizontal[x * 3 + 2] += (c & 255) * weight;
          }
        }
        cached_y = sy;
      }
      const unsigned weight =
          std::min(end, (sy + 1) * r.height) - std::max(begin, sy * r.height);
      for (size_t i = 0; i < sums.size(); ++i)
        sums[i] += uint64_t(horizontal[i]) * weight;
    }
    auto *dst = &pixels.rgb[(size_t(y + r.y) * pixels.width + r.x) * 3];
    for (size_t i = 0; i < sums.size(); ++i)
      dst[i] = (sums[i] + area / 2) / area;
  }
  return pixels;
}
std::string KittySharedUpload(const std::string &name, int width, int height,
                              bool alternate_buffer, bool query) {
  if (name.empty() || name.size() > 128 || width < 1 || width > 1920 ||
      height < 1 || height > 1080)
    throw std::invalid_argument("invalid shared image");
  return "\033_Ga=" + std::string(query ? "q" : "t") + ",t=s,f=24,q=0,i=" +
         std::to_string(query              ? 33
                        : alternate_buffer ? 32
                                           : 31) +
         ",s=" + std::to_string(width) + ",v=" + std::to_string(height) +
         ",S=" + std::to_string(size_t(width) * height * 3) + ";" +
         Base64(std::vector<uint8_t>(name.begin(), name.end())) + "\033\\";
}
std::string KittyPresent(const Viewport &v, bool alternate_buffer) {
  return "\033[" + std::to_string(v.row_offset + 1) +
         ";1H\033_Ga=p,i=" + std::to_string(alternate_buffer ? 32 : 31) +
         ",p=1,C=1,q=2,c=" + std::to_string(v.columns) +
         ",r=" + std::to_string(v.rows) +
         "\033\\\033_Ga=d,d=I,i=" + std::to_string(alternate_buffer ? 31 : 32) +
         ",q=2\033\\";
}
std::optional<KittyAck> ParseKittyAck(const std::string &s) {
  if (s.size() > 1024 || s.size() < 10 || s.compare(0, 3, "\033_G") != 0 ||
      s.compare(s.size() - 2, 2, "\033\\") != 0)
    return {};
  const auto separator = s.find(';', 3);
  if (separator == std::string::npos)
    return {};
  uint32_t image_id = 0;
  for (size_t pos = 3; pos < separator;) {
    const auto end = std::min(s.find(',', pos), separator);
    if (s.compare(pos, 2, "i=") == 0) {
      if (image_id)
        return {};
      const auto parsed =
          std::from_chars(s.data() + pos + 2, s.data() + end, image_id);
      if (parsed.ec != std::errc{} || parsed.ptr != s.data() + end || !image_id)
        return {};
    } else if (s.compare(pos, 2, "p=") == 0 &&
               s.substr(pos, end - pos) != "p=0") {
      return {}; // Only upload/query acknowledgements are relevant here.
    }
    pos = end + 1;
  }
  if (!image_id)
    return {};
  return KittyAck{image_id,
                  s.substr(separator + 1, s.size() - separator - 3) == "OK"};
}
std::string Kitty(const DecodedFrame &f, const Viewport &requested,
                  bool alternate_buffer) {
  auto v = requested;
  v.pixel_mode = true;
  Validate(f, v);
  // Use reported terminal pixel dimensions; otherwise a conservative 10x20
  // cell.
  int w =
      std::clamp(v.pixel_width > 0 ? v.pixel_width : v.columns * 10, 2, 1920);
  int h =
      std::clamp(v.pixel_height > 0 ? v.pixel_height : v.rows * 20, 2, 1080);
  const auto crop = Crop(f, v);
  std::string b64;
  for (;;) {
    auto r = Fit(crop.width, crop.height, w, h,
                 double(v.columns) * v.cell_aspect * h / (v.rows * w));
    std::vector<uint8_t> rgb(size_t(w) * h * 3, 0);
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        double x0 = crop.x + double(x) * crop.width / r.width,
               y0 = crop.y + double(y) * crop.height / r.height;
        auto c = Sample(f, x0, y0, x0 + double(crop.width) / r.width,
                        y0 + double(crop.height) / r.height);
        auto i = (size_t(y + r.y) * w + x + r.x) * 3;
        rgb[i] = c >> 16;
        rgb[i + 1] = c >> 8;
        rgb[i + 2] = c;
      }
    uLongf length = compressBound(rgb.size());
    std::vector<uint8_t> compressed(length);
    if (compress2(compressed.data(), &length, rgb.data(), rgb.size(),
                  Z_BEST_SPEED) != Z_OK)
      throw std::runtime_error("pixel compression failed");
    compressed.resize(length);
    b64 = Base64(compressed);
    // Reserve space for protocol chunk headers; keep the existing 2 MiB cap.
    if (b64.size() < 1900000)
      break;
    w = std::max(2, w * 3 / 4);
    h = std::max(2, h * 3 / 4);
  }
  const auto image_id = std::to_string(alternate_buffer ? 32 : 31);
  std::string out;
  out.reserve(b64.size() + (b64.size() / 4096 + 1) * 12 + 256);
  for (size_t i = 0; i < b64.size(); i += 4096) {
    out += "\033_G";
    if (i == 0)
      out += "a=t,t=d,f=24,o=z,q=2,i=" + image_id + ",s=" +
             std::to_string(w) + ",v=" + std::to_string(h) + ",";
    out += i + 4096 < b64.size() ? "m=1;" : "m=0;";
    out.append(b64, i, 4096);
    out += "\033\\";
  }
  // Re-transmitting a visible image ID can remove its placement as soon as
  // upload starts. Keep the old image throughout chunking/short writes, then
  // place the fully uploaded image before freeing the previous one.
  out += KittyPresent(v, alternate_buffer);
  return out;
}
bool KittyReply(const std::string &s) {
  return s.find("\033_Gi=31;OK\033\\") != std::string::npos;
}
} // namespace crossdesk::terminal
