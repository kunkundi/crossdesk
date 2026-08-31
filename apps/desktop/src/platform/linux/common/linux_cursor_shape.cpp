#include "linux_cursor_shape.h"

#include <remote_action.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__) && !defined(__APPLE__)
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#endif

namespace crossdesk {
namespace {

constexpr LinuxCursorAlias kCursorAliases[] = {
    {"help", RemoteCursorShape::help},
    {"question-arrow", RemoteCursorShape::help},
    {"left-ptr-help", RemoteCursorShape::help},
    {"whats-this", RemoteCursorShape::help},
    {"dnd-ask", RemoteCursorShape::help},
    {"pointer", RemoteCursorShape::pointer},
    {"hand", RemoteCursorShape::pointer},
    {"hand1", RemoteCursorShape::pointer},
    {"hand2", RemoteCursorShape::pointer},
    {"link", RemoteCursorShape::pointer},
    {"pointing-hand", RemoteCursorShape::pointer},
    {"progress", RemoteCursorShape::progress},
    {"left-ptr-watch", RemoteCursorShape::progress},
    {"half-busy", RemoteCursorShape::progress},
    {"wait", RemoteCursorShape::wait},
    {"watch", RemoteCursorShape::wait},
    {"crosshair", RemoteCursorShape::crosshair},
    {"cross", RemoteCursorShape::crosshair},
    {"cross-reverse", RemoteCursorShape::crosshair},
    {"tcross", RemoteCursorShape::crosshair},
    {"cell", RemoteCursorShape::crosshair},
    {"plus", RemoteCursorShape::crosshair},
    {"target", RemoteCursorShape::crosshair},
    {"dotbox", RemoteCursorShape::crosshair},
    {"dot-box-mask", RemoteCursorShape::crosshair},
    {"text", RemoteCursorShape::text},
    {"xterm", RemoteCursorShape::text},
    {"ibeam", RemoteCursorShape::text},
    {"vertical-text", RemoteCursorShape::text},
    {"alias", RemoteCursorShape::alias},
    {"dnd-link", RemoteCursorShape::alias},
    {"copy", RemoteCursorShape::copy},
    {"dnd-copy", RemoteCursorShape::copy},
    {"move", RemoteCursorShape::move},
    {"fleur", RemoteCursorShape::move},
    {"size-all", RemoteCursorShape::move},
    {"all-scroll", RemoteCursorShape::move},
    {"dnd-move", RemoteCursorShape::move},
    {"no-drop", RemoteCursorShape::no_drop},
    {"dnd-no-drop", RemoteCursorShape::no_drop},
    {"dnd-none", RemoteCursorShape::no_drop},
    {"not-allowed", RemoteCursorShape::not_allowed},
    {"forbidden", RemoteCursorShape::not_allowed},
    {"crossed-circle", RemoteCursorShape::not_allowed},
    {"circle", RemoteCursorShape::not_allowed},
    {"pirate", RemoteCursorShape::not_allowed},
    {"grab", RemoteCursorShape::grab},
    {"openhand", RemoteCursorShape::grab},
    {"open-hand", RemoteCursorShape::grab},
    {"grabbing", RemoteCursorShape::grabbing},
    {"closedhand", RemoteCursorShape::grabbing},
    {"closed-hand", RemoteCursorShape::grabbing},
    {"col-resize", RemoteCursorShape::col_resize},
    {"row-resize", RemoteCursorShape::row_resize},
    {"n-resize", RemoteCursorShape::n_resize},
    {"top-side", RemoteCursorShape::n_resize},
    {"top-tee", RemoteCursorShape::n_resize},
    {"sb-up-arrow", RemoteCursorShape::n_resize},
    {"e-resize", RemoteCursorShape::e_resize},
    {"right-side", RemoteCursorShape::e_resize},
    {"right-tee", RemoteCursorShape::e_resize},
    {"sb-right-arrow", RemoteCursorShape::e_resize},
    {"s-resize", RemoteCursorShape::s_resize},
    {"bottom-side", RemoteCursorShape::s_resize},
    {"bottom-tee", RemoteCursorShape::s_resize},
    {"sb-down-arrow", RemoteCursorShape::s_resize},
    {"w-resize", RemoteCursorShape::w_resize},
    {"left-side", RemoteCursorShape::w_resize},
    {"left-tee", RemoteCursorShape::w_resize},
    {"sb-left-arrow", RemoteCursorShape::w_resize},
    {"ne-resize", RemoteCursorShape::ne_resize},
    {"top-right-corner", RemoteCursorShape::ne_resize},
    {"ur-angle", RemoteCursorShape::ne_resize},
    {"nw-resize", RemoteCursorShape::nw_resize},
    {"top-left-corner", RemoteCursorShape::nw_resize},
    {"ul-angle", RemoteCursorShape::nw_resize},
    {"se-resize", RemoteCursorShape::se_resize},
    {"bottom-right-corner", RemoteCursorShape::se_resize},
    {"lr-angle", RemoteCursorShape::se_resize},
    {"sw-resize", RemoteCursorShape::sw_resize},
    {"bottom-left-corner", RemoteCursorShape::sw_resize},
    {"ll-angle", RemoteCursorShape::sw_resize},
    {"ew-resize", RemoteCursorShape::ew_resize},
    {"sb-h-double-arrow", RemoteCursorShape::ew_resize},
    {"h-double-arrow", RemoteCursorShape::ew_resize},
    {"size-hor", RemoteCursorShape::ew_resize},
    {"split-h", RemoteCursorShape::ew_resize},
    {"exchange", RemoteCursorShape::ew_resize},
    {"ns-resize", RemoteCursorShape::ns_resize},
    {"sb-v-double-arrow", RemoteCursorShape::ns_resize},
    {"v-double-arrow", RemoteCursorShape::ns_resize},
    {"double-arrow", RemoteCursorShape::ns_resize},
    {"size-ver", RemoteCursorShape::ns_resize},
    {"split-v", RemoteCursorShape::ns_resize},
    {"nesw-resize", RemoteCursorShape::nesw_resize},
    {"fd-double-arrow", RemoteCursorShape::nesw_resize},
    {"size-bdiag", RemoteCursorShape::nesw_resize},
    {"nwse-resize", RemoteCursorShape::nwse_resize},
    {"bd-double-arrow", RemoteCursorShape::nwse_resize},
    {"size-fdiag", RemoteCursorShape::nwse_resize},
    {"default", RemoteCursorShape::default_cursor},
    {"left-ptr", RemoteCursorShape::default_cursor},
    {"left-arrow", RemoteCursorShape::default_cursor},
    {"arrow", RemoteCursorShape::default_cursor},
    {"top-left-arrow", RemoteCursorShape::default_cursor},
    {"right-ptr", RemoteCursorShape::default_cursor},
    {"center-ptr", RemoteCursorShape::default_cursor},
    {"context-menu", RemoteCursorShape::default_cursor},
};

std::string NormalizeCursorName(const std::string& raw_name) {
  std::string normalized;
  normalized.reserve(raw_name.size());
  for (unsigned char ch : raw_name) {
    if (ch == '_' || std::isspace(ch)) {
      normalized.push_back('-');
    } else {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return normalized;
}

#if defined(__linux__) && !defined(__APPLE__)

struct CursorCandidate {
  RemoteCursorShape shape = RemoteCursorShape::default_cursor;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t xhot = 0;
  uint32_t yhot = 0;
  std::vector<uint32_t> argb;
};

uint8_t Alpha(uint32_t argb) {
  return static_cast<uint8_t>((argb >> 24U) & 0xffU);
}

double BilinearAlpha(const CursorCandidate& candidate, double x, double y) {
  if (candidate.width == 0 || candidate.height == 0) {
    return 0.0;
  }

  x = std::max(0.0, std::min(x, static_cast<double>(candidate.width - 1)));
  y = std::max(0.0, std::min(y, static_cast<double>(candidate.height - 1)));
  const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
  const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
  const uint32_t x1 = std::min(x0 + 1, candidate.width - 1);
  const uint32_t y1 = std::min(y0 + 1, candidate.height - 1);
  const double fx = x - x0;
  const double fy = y - y0;

  const auto sample = [&](uint32_t px, uint32_t py) {
    return static_cast<double>(
        Alpha(candidate.argb[static_cast<size_t>(py) * candidate.width + px]));
  };
  const double top = sample(x0, y0) * (1.0 - fx) + sample(x1, y0) * fx;
  const double bottom = sample(x0, y1) * (1.0 - fx) + sample(x1, y1) * fx;
  return top * (1.0 - fy) + bottom * fy;
}

double SimilarityError(const LinuxCursorImageView& image,
                       const CursorCandidate& candidate) {
  if (!image.argb || image.width == 0 || image.height == 0 ||
      candidate.argb.empty()) {
    return std::numeric_limits<double>::infinity();
  }

  double alpha_error = 0.0;
  for (uint32_t y = 0; y < image.height; ++y) {
    const double source_y =
        (static_cast<double>(y) + 0.5) * candidate.height / image.height - 0.5;
    for (uint32_t x = 0; x < image.width; ++x) {
      const double source_x = (static_cast<double>(x) + 0.5) *
                                  candidate.width / image.width -
                              0.5;
      const double expected = BilinearAlpha(candidate, source_x, source_y);
      const double actual = static_cast<double>(
          Alpha(image.argb[static_cast<size_t>(y) * image.width + x]));
      alpha_error += std::abs(actual - expected) / 255.0;
    }
  }
  alpha_error /= static_cast<double>(image.width) * image.height;

  const double observed_hot_x =
      static_cast<double>(image.xhot) / std::max(1u, image.width);
  const double observed_hot_y =
      static_cast<double>(image.yhot) / std::max(1u, image.height);
  const double candidate_hot_x =
      static_cast<double>(candidate.xhot) / std::max(1u, candidate.width);
  const double candidate_hot_y =
      static_cast<double>(candidate.yhot) / std::max(1u, candidate.height);
  const double hotspot_error =
      std::abs(observed_hot_x - candidate_hot_x) +
      std::abs(observed_hot_y - candidate_hot_y);

  const double observed_aspect =
      static_cast<double>(image.width) / image.height;
  const double candidate_aspect =
      static_cast<double>(candidate.width) / candidate.height;
  const double aspect_error =
      std::abs(std::log(observed_aspect / candidate_aspect));

  return alpha_error + hotspot_error * 0.12 + aspect_error * 0.08;
}

bool SameImage(const LinuxCursorImageView& image,
               const CursorCandidate& candidate) {
  if (image.width != candidate.width || image.height != candidate.height ||
      image.xhot != candidate.xhot || image.yhot != candidate.yhot) {
    return false;
  }
  const size_t pixel_count =
      static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  return std::equal(image.argb, image.argb + pixel_count,
                    candidate.argb.begin());
}

#endif

}  // namespace

const LinuxCursorAlias* LinuxCursorAliases(size_t* count) {
  if (count) {
    *count = sizeof(kCursorAliases) / sizeof(kCursorAliases[0]);
  }
  return kCursorAliases;
}

RemoteCursorShape ShapeFromLinuxCursorName(const std::string& raw_name) {
  const std::string name = NormalizeCursorName(raw_name);
  for (const auto& alias : kCursorAliases) {
    if (name == alias.name) {
      return alias.shape;
    }
  }

  // Some toolkits prefix a theme or toolkit name to the standard cursor name.
  // Restrict the fallback to separators so names such as "pointer-progress"
  // still prefer their exact semantic alias above.
  for (const auto& alias : kCursorAliases) {
    const std::string suffix = std::string("-") + alias.name;
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return alias.shape;
    }
  }
  return RemoteCursorShape::default_cursor;
}

#if defined(__linux__) && !defined(__APPLE__)

struct LinuxCursorThemeMatcher::Impl {
  Display* display = XOpenDisplay(nullptr);
  std::string theme;
  int default_size = 24;
  std::unordered_map<int, std::vector<CursorCandidate>> candidates_by_size;

  Impl() {
    if (!display) {
      return;
    }
    if (const char* active_theme = XcursorGetTheme(display)) {
      theme = active_theme;
    }
    const int active_size = XcursorGetDefaultSize(display);
    if (active_size > 0) {
      default_size = active_size;
    }
  }

  ~Impl() {
    if (display) {
      XCloseDisplay(display);
    }
  }

  const std::vector<CursorCandidate>& Candidates(int requested_size) {
    requested_size = std::max(1, requested_size);
    auto existing = candidates_by_size.find(requested_size);
    if (existing != candidates_by_size.end()) {
      return existing->second;
    }

    std::vector<CursorCandidate> candidates;
    const char* theme_name = theme.empty() ? nullptr : theme.c_str();
    for (const auto& alias : kCursorAliases) {
      XcursorImages* images =
          XcursorLibraryLoadImages(alias.name, theme_name, requested_size);
      if (!images) {
        continue;
      }
      for (int index = 0; index < images->nimage; ++index) {
        const XcursorImage* image = images->images[index];
        if (!image || !image->pixels || image->width == 0 ||
            image->height == 0) {
          continue;
        }

        CursorCandidate candidate;
        candidate.shape = alias.shape;
        candidate.width = image->width;
        candidate.height = image->height;
        candidate.xhot = image->xhot;
        candidate.yhot = image->yhot;
        const size_t pixel_count = static_cast<size_t>(image->width) *
                                   static_cast<size_t>(image->height);
        candidate.argb.assign(image->pixels, image->pixels + pixel_count);

        const bool duplicate =
            std::any_of(candidates.begin(), candidates.end(),
                        [&](const CursorCandidate& current) {
                          return current.shape == candidate.shape &&
                                 current.width == candidate.width &&
                                 current.height == candidate.height &&
                                 current.xhot == candidate.xhot &&
                                 current.yhot == candidate.yhot &&
                                 current.argb == candidate.argb;
                        });
        if (!duplicate) {
          candidates.push_back(std::move(candidate));
        }
      }
      XcursorImagesDestroy(images);
    }

    return candidates_by_size
        .emplace(requested_size, std::move(candidates))
        .first->second;
  }
};

#else

struct LinuxCursorThemeMatcher::Impl {};

#endif

LinuxCursorThemeMatcher::LinuxCursorThemeMatcher()
    : impl_(std::make_unique<Impl>()) {}

LinuxCursorThemeMatcher::~LinuxCursorThemeMatcher() = default;

RemoteCursorShape LinuxCursorThemeMatcher::Match(
    const LinuxCursorImageView& image) {
#if defined(__linux__) && !defined(__APPLE__)
  if (!impl_ || !image.argb || image.width == 0 || image.height == 0) {
    return RemoteCursorShape::default_cursor;
  }

  const int requested_size =
      static_cast<int>(std::max(image.width, image.height));
  const auto& candidates = impl_->Candidates(requested_size);
  for (const auto& candidate : candidates) {
    if (SameImage(image, candidate)) {
      return candidate.shape;
    }
  }

  double best_error = std::numeric_limits<double>::infinity();
  RemoteCursorShape best_shape = RemoteCursorShape::default_cursor;
  for (const auto& candidate : candidates) {
    const double error = SimilarityError(image, candidate);
    if (error < best_error) {
      best_error = error;
      best_shape = candidate.shape;
    }
  }

  // Scaled theme cursors normally remain well below this threshold, while an
  // unrelated custom application cursor should fall back to the default.
  return best_error <= 0.20 ? best_shape
                            : RemoteCursorShape::default_cursor;
#else
  (void)image;
  return RemoteCursorShape::default_cursor;
#endif
}

}  // namespace crossdesk
