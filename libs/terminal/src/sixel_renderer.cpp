#include "terminal_renderer.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>

namespace crossdesk::terminal {
Viewport SixelViewport(Viewport v) {
  if (v.columns < 1 || v.columns > 240 || v.rows < 1 || v.rows > 100)
    throw std::invalid_argument("invalid Sixel viewport");
  const auto limit = [](int &cells, int &pixels, int fallback, int maximum) {
    const int original_cells = cells;
    if (pixels <= 0)
      pixels = cells * fallback;
    cells =
        std::max(1, std::min(cells, int(int64_t(maximum) * cells / pixels)));
    pixels = std::clamp(
        int((int64_t(pixels) * cells + original_cells / 2) / original_cells), 2,
        maximum);
  };
  limit(v.columns, v.pixel_width, 10, 1920);
  limit(v.rows, v.pixel_height, 20, 1080);
  v.pixel_mode = true;
  return v;
}

bool SixelReply(const std::string &response) {
  for (size_t pos = 0;
       (pos = response.find("\033[?", pos)) != std::string::npos; ++pos) {
    const auto end = response.find('c', pos + 3);
    if (end == std::string::npos || end - pos > 1024)
      continue;
    const char *cur = response.data() + pos + 3;
    const char *last = response.data() + end;
    bool first = true, supported = false;
    while (cur < last) {
      unsigned value = 0;
      const auto parsed = std::from_chars(cur, last, value);
      if (parsed.ec != std::errc{})
        break;
      // The first parameter identifies the terminal, not a capability.
      supported = supported || (!first && value == 4);
      first = false;
      cur = parsed.ptr;
      if (cur == last) {
        if (supported)
          return true;
        break;
      }
      if (*cur++ != ';')
        break;
    }
  }
  return false;
}

namespace {
struct Palette {
  int levels, grays;
  std::vector<std::array<int, 3>> colors;
  Palette(int cube_levels, int gray_levels)
      : levels(cube_levels), grays(gray_levels) {
    for (int r = 0; r < levels; ++r)
      for (int g = 0; g < levels; ++g)
        for (int b = 0; b < levels; ++b)
          colors.push_back({Level(r), Level(g), Level(b)});
    for (int i = 0; i < grays; ++i) {
      const int value = Gray(i);
      colors.push_back({value, value, value});
    }
  }
  int Level(int i) const { return (i * 255 + (levels - 1) / 2) / (levels - 1); }
  int Gray(int i) const { return (i * 255 + (grays - 1) / 2) / (grays - 1); }
  uint8_t Index(const uint8_t *rgb) const {
    const int gray =
        ((int(rgb[0]) + rgb[1] + rgb[2]) * (grays - 1) + 382) / 765;
    const int gray_index = levels * levels * levels + gray;
    if (!levels)
      return uint8_t(gray_index);
    int index = 0, cube_error = 0, gray_error = 0;
    for (int c = 0; c < 3; ++c) {
      const int step = (rgb[c] * (levels - 1) + 127) / 255;
      index = index * levels + step;
      const int dc = rgb[c] - Level(step), dg = rgb[c] - Gray(gray);
      cube_error += dc * dc;
      gray_error += dg * dg;
    }
    return uint8_t(gray_error < cube_error ? gray_index : index);
  }
};

void Run(std::string &out, uint8_t bits, int count) {
  if (count > 3) {
    out += '!';
    out += std::to_string(count);
    out += char('?' + bits);
  } else {
    out.append(size_t(count), char('?' + bits));
  }
}

std::string Encode(const PixelFrame &pixels, const Viewport &v,
                   const Palette &palette) {
  constexpr size_t budget = 1900000; // Leave room for terminal/UI commands.
  const int width = pixels.width, height = pixels.height;
  std::string out = "\0337\033[" + std::to_string(v.row_offset + 1) +
                    ";1H\033P0;1;0q\"1;1;" + std::to_string(width) + ";" +
                    std::to_string(height);
  // Define a complete palette for every frame, including black margins.
  // Pb=1 preserves pixels outside the image in its final partial six-row band.
  for (size_t i = 0; i < palette.colors.size(); ++i) {
    out += '#' + std::to_string(i) + ";2";
    for (int c : palette.colors[i])
      out += ';' + std::to_string((c * 100 + 127) / 255);
  }
  std::vector<uint8_t> planes(palette.colors.size() * width);
  std::vector<int> ends(palette.colors.size());
  for (int y = 0; y < height; y += 6) {
    std::fill(planes.begin(), planes.end(), 0);
    std::fill(ends.begin(), ends.end(), 0);
    for (int dy = 0; dy < std::min(6, height - y); ++dy)
      for (int x = 0; x < width; ++x) {
        const auto color =
            palette.Index(&pixels.rgb[(size_t(y + dy) * width + x) * 3]);
        planes[size_t(color) * width + x] |= uint8_t(1 << dy);
        ends[color] = std::max(ends[color], x + 1);
      }
    bool first = true;
    for (size_t color = 0; color < ends.size(); ++color) {
      if (!ends[color])
        continue;
      if (!first)
        out += '$'; // Return to the left edge without advancing the band.
      first = false;
      out += '#' + std::to_string(color);
      const auto *plane = &planes[color * width];
      for (int x = 0; x < ends[color];) {
        int end = x + 1;
        while (end < ends[color] && plane[end] == plane[x])
          ++end;
        Run(out, plane[x], end - x);
        x = end;
      }
      if (out.size() > budget)
        return {};
    }
    if (y + 6 < height)
      out += '-';
  }
  out += "\033\\\0338";
  return out;
}
} // namespace

std::string Sixel(const DecodedFrame &frame, const Viewport &viewport) {
  const auto v = SixelViewport(viewport);
  const auto pixels = ImagePixels(frame, v);
  // Sixel cannot scale a smaller bitmap to a cell placement. Reduce color
  // detail on high-entropy frames instead of shrinking or misplacing them.
  for (const auto &sizes :
       {std::pair{6, 32}, std::pair{3, 16}, std::pair{2, 8}, std::pair{0, 4}}) {
    auto out = Encode(pixels, v, Palette(sizes.first, sizes.second));
    if (!out.empty())
      return out;
  }
  // Four planes at 1920 x 1080 always fit the budget above.
  throw std::runtime_error("Sixel output budget exceeded");
}
} // namespace crossdesk::terminal
