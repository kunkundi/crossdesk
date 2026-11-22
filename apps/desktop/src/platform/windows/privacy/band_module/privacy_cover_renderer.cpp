/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "privacy_cover_renderer.h"

#include <algorithm>
#include <array>

namespace crossdesk {
namespace {

constexpr size_t kInstructionLines = 2;
constexpr size_t kBrandLine = kInstructionLines;

struct PaintResources {
  HDC dc;
  int saved;
  HICON logo = nullptr;
  HBRUSH key_fill = nullptr;
  HPEN key_border = nullptr;
  std::array<HFONT, kInstructionLines + 1> fonts{};
  ~PaintResources() {
    // Deselect every owned GDI object before releasing it.
    if (saved) RestoreDC(dc, saved);
    for (auto font : fonts)
      if (font) DeleteObject(font);
    if (logo) DestroyIcon(logo);
    if (key_fill) DeleteObject(key_fill);
    if (key_border) DeleteObject(key_border);
  }
};

bool Select(HDC dc, HGDIOBJ object) {
  const auto previous = SelectObject(dc, object);
  return previous && previous != HGDI_ERROR;
}

}  // namespace

bool DrawPrivacyCover(HDC dc, const RECT& rect, UINT dpi, HINSTANCE module,
                      const wchar_t* unlock_hint, PrivacyCoverStyle style) {
  if (!FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))))
    return false;
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) return false;
  if (style == PrivacyCoverStyle::black) return true;
  const bool allow_local_unlock = style == PrivacyCoverStyle::unlock_hint;
  // Scale with the monitor DPI, shrinking the whole group on small displays.
  // All coordinates remain in the owning window's physical-pixel context.
  const double scale = (std::min)({dpi / 96.0, width / 960.0, height / 280.0});
  const auto px = [scale](int value) {
    return (std::max)(1, static_cast<int>(value * scale + 0.5));
  };
  const int margin = px(32);
  const int text_width = px(420);
  const int line_gap = px(16);
  PaintResources resources{dc, SaveDC(dc)};
  if (!resources.saved || !SetBkMode(dc, TRANSPARENT)) return false;

  struct Line {
    const wchar_t* text;
    int size;
    int weight;
    COLORREF color;
    RECT bounds{};
  };
  std::array<Line, kInstructionLines + 1> lines{{
      {unlock_hint, 17, FW_NORMAL, RGB(166, 174, 186)},
      {L"+", 16, FW_SEMIBOLD, RGB(229, 233, 240)},
      {L" CrossDesk 远程控制中", 29, FW_SEMIBOLD, RGB(255, 255, 255)},
  }};
  constexpr UINT flags = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;
  constexpr UINT brand_flags = DT_LEFT | DT_SINGLELINE | DT_NOPREFIX;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i < kInstructionLines && !allow_local_unlock) continue;
    auto& line = lines[i];
    resources.fonts[i] =
        CreateFontW(-px(line.size), 0, 0, 0, line.weight, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (!resources.fonts[i] || !Select(dc, resources.fonts[i])) return false;
    line.bounds = {0, 0, text_width, 0};
    if (!DrawTextW(dc, line.text, -1, &line.bounds,
                   (i == 0 ? flags : brand_flags) | DT_CALCRECT) ||
        line.bounds.right > text_width)
      return false;
  }
  // Keep the logo and status text side by side, aligned along their bottom edge.
  const int logo_size = px(60);
  const int brand_gap = px(8);
  const int brand_width = logo_size + brand_gap +
      static_cast<int>(lines[kBrandLine].bounds.right);
  const int brand_height = (std::max)(logo_size,
      static_cast<int>(lines[kBrandLine].bounds.bottom));
  if (logo_size <= 0 ||
      brand_height + 2 * margin > height ||
      brand_width + 2 * margin > width)
    return false;
  resources.logo = static_cast<HICON>(LoadImageW(
      module, L"IDI_CROSSDESK_LOGO", IMAGE_ICON, logo_size, logo_size, 0));
  if (!resources.logo) return false;
  const int brand_left = rect.left + margin;
  // Keep the corner branding fixed when local unlocking is toggled.
  const int brand_bottom = rect.bottom - margin;
  if (!DrawIconEx(dc, brand_left, brand_bottom - logo_size,
                  resources.logo, logo_size,
                  logo_size, 0, nullptr, DI_NORMAL))
    return false;
  const int brand_text_left = brand_left + logo_size + brand_gap;
  const auto& brand = lines[kBrandLine];
  if (!Select(dc, resources.fonts[kBrandLine])) return false;
  RECT brand_bounds{brand_text_left, brand_bottom - brand.bounds.bottom,
                    brand_text_left + brand.bounds.right, brand_bottom};
  SetTextColor(dc, brand.color);
  if (!DrawTextW(dc, brand.text, -1, &brand_bounds, brand_flags)) return false;
  if (!allow_local_unlock) return true;

  constexpr std::array<const wchar_t*, 4> keys{L"Ctrl", L"Alt", L"Shift", L"F12"};
  std::array<int, keys.size()> key_widths{};
  const int key_gap = px(28);
  const int key_height = (std::max)(px(44),
      static_cast<int>(lines[1].bounds.bottom) + 2 * px(10));
  int row_width = key_gap * static_cast<int>(keys.size() - 1);
  if (!Select(dc, resources.fonts[1])) return false;
  for (size_t i = 0; i < keys.size(); ++i) {
    SIZE extent{};
    if (!GetTextExtentPoint32W(dc, keys[i], lstrlenW(keys[i]), &extent)) return false;
    key_widths[i] = (std::max)(px(48), static_cast<int>(extent.cx) + 2 * px(14));
    row_width += key_widths[i];
  }
  const int content_height = static_cast<int>(lines[0].bounds.bottom) + line_gap + key_height;
  // Center the instructions independently of the footer, leaving room on short displays.
  const int top = (std::min)(static_cast<int>(rect.top) + (height - content_height) / 2,
      brand_bottom - brand_height - px(32) - content_height);
  if (top < rect.top + margin || (std::max)(row_width, text_width) + 2 * margin > width)
    return false;
  const int hint_left = rect.left + (width - text_width) / 2;
  RECT hint_bounds{hint_left, top, hint_left + text_width, top + lines[0].bounds.bottom};
  if (!Select(dc, resources.fonts[0])) return false;
  SetTextColor(dc, lines[0].color);
  if (!DrawTextW(dc, unlock_hint, -1, &hint_bounds, flags)) return false;

  resources.key_fill = CreateSolidBrush(RGB(16, 19, 24));
  resources.key_border = CreatePen(PS_SOLID, px(1), RGB(52, 58, 68));
  if (!resources.key_fill || !resources.key_border ||
      !Select(dc, resources.key_fill) || !Select(dc, resources.key_border) ||
      !Select(dc, resources.fonts[1]))
    return false;
  const int key_top = top + static_cast<int>(lines[0].bounds.bottom) + line_gap;
  int left = rect.left + (width - row_width) / 2;
  constexpr UINT key_flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
  for (size_t i = 0; i < keys.size(); ++i) {
    RECT bounds{left, key_top, left + key_widths[i], key_top + key_height};
    if (!RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, px(16), px(16)))
      return false;
    SetTextColor(dc, lines[1].color);
    if (!DrawTextW(dc, keys[i], -1, &bounds, key_flags)) return false;
    left += key_widths[i];
    if (i + 1 < keys.size()) {
      RECT plus_bounds{left, key_top, left + key_gap, key_top + key_height};
      SetTextColor(dc, lines[0].color);
      if (!DrawTextW(dc, L"+", -1, &plus_bounds, key_flags)) return false;
      left += key_gap;
    }
  }
  return true;
}

}  // namespace crossdesk
