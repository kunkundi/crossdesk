#include "terminal_renderer.h"
#include <algorithm>
#include <array>
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
static Cell Quadrant(const std::array<uint32_t, 4> &pixels) {
  // Enumerate all seven two-colour partitions (complements are equivalent).
  static constexpr uint32_t glyphs[16] = {
      0x20,   0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
      0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588};
  Cell best{};
  double best_error = 1e30;
  for (unsigned mask = 1; mask < 8; ++mask) {
    double sums[2][3] = {};
    unsigned count[2] = {};
    for (int i = 0; i < 4; ++i) {
      int g = (mask >> i) & 1;
      ++count[g];
      sums[g][0] += pixels[i] >> 16;
      sums[g][1] += (pixels[i] >> 8) & 255;
      sums[g][2] += pixels[i] & 255;
    }
    uint32_t colors[2] = {};
    double error = 0;
    for (int g = 0; g < 2; ++g)
      for (int c = 0; c < 3; ++c) {
        sums[g][c] = std::round(sums[g][c] / count[g]);
        colors[g] |= uint32_t(sums[g][c]) << (16 - 8 * c);
      }
    for (int i = 0; i < 4; ++i)
      for (int c = 0; c < 3; ++c) {
        double d = double((pixels[i] >> (16 - 8 * c)) & 255) -
                   sums[(mask >> i) & 1][c];
        error += d * d;
      }
    if (error < best_error) {
      best_error = error;
      best = {colors[1], colors[0], glyphs[mask]};
    }
  }
  if (best.top == best.bottom)
    best.glyph = 0x20;
  return best;
}
static std::string EncodeUtf8(uint32_t c) {
  if (c < 128)
    return std::string(1, char(c));
  if (c < 2048)
    return {char(0xc0 | (c >> 6)), char(0x80 | (c & 63))};
  return {char(0xe0 | (c >> 12)), char(0x80 | ((c >> 6) & 63)),
          char(0x80 | (c & 63))};
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
      auto &c = g.cells[size_t(y) * v.columns + x];
      if (sx == 2)
        c = Quadrant({pixel(x * 2, y * 2), pixel(x * 2 + 1, y * 2),
                      pixel(x * 2, y * 2 + 1), pixel(x * 2 + 1, y * 2 + 1)});
      else {
        c.top = pixel(x, y * sy);
        c.bottom = sy == 1 ? c.top : pixel(x, y * sy + sy - 1);
      }
    }
  return g;
}
std::optional<std::pair<float, float>> MapPointer(const Grid &g, int x, int y) {
  x *= g.samples_x;
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
static std::string Color(uint32_t c, bool fg, int depth) {
  const int r = c >> 16, g = (c >> 8) & 255, b = c & 255;
  const std::string prefix = fg ? "\033[38;" : "\033[48;";
  if (depth == 24)
    return prefix + "2;" + std::to_string(r) + ";" + std::to_string(g) + ";" +
           std::to_string(b) + "m";
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
    return prefix + "5;" +
           std::to_string(gray_error < cube ? 232 + gray
                                            : 16 + 36 * ri + 6 * gi + bi) +
           "m";
  }
  int i = (r >= 128 ? 1 : 0) + (g >= 128 ? 2 : 0) + (b >= 128 ? 4 : 0);
  return "\033[" + std::to_string((fg ? 30 : 40) + i) + "m";
}
static std::string Line(const Grid &g, int y) {
  std::string line;
  uint32_t fg = UINT32_MAX, bg = UINT32_MAX;
  for (int x = 0; x < g.viewport.columns; ++x) {
    const auto &c = g.cells[size_t(y) * g.viewport.columns + x];
    if (g.viewport.utf8 && fg != c.top) {
      line += Color(c.top, true, g.viewport.colors);
      fg = c.top;
    }
    const auto bottom = g.viewport.utf8 ? c.bottom : c.top;
    if (bg != bottom) {
      line += Color(bottom, false, g.viewport.colors);
      bg = bottom;
    }
    line += g.viewport.utf8 ? EncodeUtf8(c.glyph) : " ";
  }
  return line + "\033[0m";
}
std::vector<std::string> Lines(const Grid &g) {
  std::vector<std::string> lines;
  lines.reserve(g.viewport.rows);
  for (int y = 0; y < g.viewport.rows; ++y)
    lines.push_back(Line(g, y));
  return lines;
}
std::string Difference(const Grid &g, const std::optional<Grid> &previous) {
  std::string out;
  const bool same = previous &&
                    previous->viewport.columns == g.viewport.columns &&
                    previous->viewport.rows == g.viewport.rows &&
                    previous->viewport.colors == g.viewport.colors &&
                    previous->viewport.utf8 == g.viewport.utf8;
  for (int y = 0; y < g.viewport.rows; ++y) {
    auto start = g.cells.begin() + size_t(y) * g.viewport.columns;
    if (!same ||
        !std::equal(start, start + g.viewport.columns,
                    previous->cells.begin() + size_t(y) * g.viewport.columns))
      out += "\033[" + std::to_string(y + 1) + ";1H" + Line(g, y);
  }
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
std::string Kitty(const DecodedFrame &f, const Viewport &requested) {
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
  std::string out = "\033[H";
  for (size_t i = 0; i < b64.size(); i += 4096) {
    out += "\033_G";
    if (i == 0)
      out += "a=T,t=d,f=24,o=z,q=2,i=31,p=1,C=1,s=" + std::to_string(w) +
             ",v=" + std::to_string(h) + ",c=" + std::to_string(v.columns) +
             ",r=" + std::to_string(v.rows) + ",";
    out += "m=" + std::to_string(i + 4096 < b64.size() ? 1 : 0) + ";" +
           b64.substr(i, 4096) + "\033\\";
  }
  return out;
}
bool KittyReply(const std::string &s) {
  return s.find("\033_Gi=31;OK\033\\") != std::string::npos;
}
} // namespace crossdesk::terminal
