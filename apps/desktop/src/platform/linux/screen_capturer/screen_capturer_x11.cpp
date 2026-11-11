#include "screen_capturer_x11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

#include <display_stream_id.h>
#include "libyuv.h"
#include "rd_log.h"
#include "x11_error_trap.h"

namespace crossdesk {

ScreenCapturerX11::ScreenCapturerX11() {}

ScreenCapturerX11::~ScreenCapturerX11() { Destroy(); }

int ScreenCapturerX11::Init(const int fps, cb_desktop_data cb) {
  Destroy();
  if (!cb) return -1;

  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    LOG_ERROR("Cannot connect to X server");
    return -1;
  }

  root_ = DefaultRootWindow(display_);
  XWindowAttributes attr{};
  if (!XGetWindowAttributes(display_, root_, &attr)) {
    LOG_ERROR("Failed to get X11 root window geometry");
    Destroy();
    return -1;
  }
  width_ = attr.width & ~1;
  height_ = attr.height & ~1;
  if (width_ <= 1 || height_ <= 1) {
    LOG_ERROR("Invalid capture size after alignment: {}x{}", width_, height_);
    Destroy();
    return -2;
  }

  int event_base = 0, error_base = 0;
  if (XRRQueryExtension(display_, &event_base, &error_base)) {
    randr_event_base_ = event_base;
    XRRSelectInput(display_, root_, RRScreenChangeNotifyMask |
                      RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);
  }
  RefreshDisplays();

  fps_ = fps;
  callback_ = cb;

  y_plane_.resize(width_ * height_);
  uv_plane_.resize((width_ / 2) * (height_ / 2) * 2);

  if (!ProbeCapture()) {
    LOG_ERROR("X11 backend probe failed, XGetImage is not usable");
    Destroy();
    return -3;
  }

  return 0;
}

int ScreenCapturerX11::Destroy() {
  Stop();

  y_plane_.clear();
  uv_plane_.clear();
  {
    std::lock_guard lock(display_info_mutex_);
    display_info_list_.clear();
  }
  monitor_index_ = initial_monitor_index_ = 0;
  root_ = 0;
  callback_ = nullptr;

  randr_event_base_ = -1;

  if (display_) {
    XCloseDisplay(display_);
    display_ = nullptr;
  }
  return 0;
}

int ScreenCapturerX11::Start(bool show_cursor) {
  if (running_) return 0;
  if (!display_ || display_info_list_.empty() || !callback_) return -1;
  show_cursor_ = show_cursor;
  running_ = true;
  paused_ = false;
  capture_error_count_ = 0;
  thread_ = std::thread([this]() {
    using clock = std::chrono::steady_clock;
    const auto frame_interval =
        std::chrono::milliseconds(std::max(1, 1000 / std::max(1, fps_)));

    while (running_) {
      const auto frame_start = clock::now();
      HandleDisplayEvents();
      if (!paused_) {
        OnFrame();
      }

      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() -
                                                                frame_start);
      if (elapsed < frame_interval) {
        std::this_thread::sleep_for(frame_interval - elapsed);
      }
    }
  });
  return 0;
}

int ScreenCapturerX11::Stop() {
  if (!running_) return 0;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  return 0;
}

int ScreenCapturerX11::Pause(int monitor_index) {
  paused_ = true;
  return 0;
}

int ScreenCapturerX11::Resume(int monitor_index) {
  paused_ = false;
  return 0;
}

int ScreenCapturerX11::SwitchTo(int monitor_index) {
  std::lock_guard lock(display_info_mutex_);
  if (monitor_index < 0 ||
      monitor_index >= static_cast<int>(display_info_list_.size())) return -1;
  monitor_index_ = monitor_index;
  return 0;
}

int ScreenCapturerX11::ResetToInitialMonitor() {
  monitor_index_ = initial_monitor_index_;
  return 0;
}
std::vector<DisplayInfo> ScreenCapturerX11::GetDisplayInfoList() {
  std::lock_guard lock(display_info_mutex_);
  return display_info_list_;
}

void ScreenCapturerX11::HandleDisplayEvents() {
  bool changed = false;
  while (XPending(display_)) {
    XEvent event{};
    XNextEvent(display_, &event);
    if (randr_event_base_ >= 0 &&
        (event.type == randr_event_base_ + RRScreenChangeNotify ||
         event.type == randr_event_base_ + RRNotify)) {
      XRRUpdateConfiguration(&event);
      changed = true;
    }
  }
  if (changed) RefreshDisplays();
}

void ScreenCapturerX11::RefreshDisplays() {
  ScopedX11ErrorTrap trap(display_);
  XWindowAttributes attr{};
  if (!XGetWindowAttributes(display_, root_, &attr)) return;
  std::vector<DisplayInfo> latest;
  auto* resources = randr_event_base_ >= 0
                        ? XRRGetScreenResources(display_, root_)
                        : nullptr;
  if (resources) {
    const auto primary = XRRGetOutputPrimary(display_, root_);
    for (int i = 0; i < resources->noutput; ++i) {
      const auto id = resources->outputs[i];
      auto* output = XRRGetOutputInfo(display_, resources, id);
      if (!output) continue;
      if (output->connection == RR_Connected && output->crtc) {
        auto* crtc = XRRGetCrtcInfo(display_, resources, output->crtc);
        if (crtc) {
          if (crtc->mode && crtc->width > 1 && crtc->height > 1 &&
              crtc->x >= 0 && crtc->y >= 0 &&
              crtc->x + crtc->width <= static_cast<unsigned>(attr.width) &&
              crtc->y + crtc->height <= static_cast<unsigned>(attr.height)) {
            latest.emplace_back(reinterpret_cast<void*>(id),
                                std::string(output->name, output->nameLen),
                                id == primary, crtc->x, crtc->y,
                                crtc->x + crtc->width, crtc->y + crtc->height);
          }
          XRRFreeCrtcInfo(crtc);
        }
      }
      XRRFreeOutputInfo(output);
    }
    XRRFreeScreenResources(resources);
  }
  if (trap.SyncAndGetError()) return;
  std::lock_guard lock(display_info_mutex_);
  if (display_info_list_.empty()) {
    if (latest.empty()) {
      latest.emplace_back(nullptr, "Virtual Display", true, 0, 0,
                          attr.width & ~1, attr.height & ~1);
    }
    display_info_list_ = std::move(latest);
    return;
  }
  // Keep stream slots stable across mode changes and unplug/replug. Newly
  // attached outputs need peer stream registration before they can be used.
  for (auto& existing : display_info_list_) {
    if (!existing.handle) {
      existing.right = existing.width = attr.width & ~1;
      existing.bottom = existing.height = attr.height & ~1;
      continue;
    }
    const auto found = std::find_if(latest.begin(), latest.end(),
        [&](const auto& item) {
          return item.handle == existing.handle && item.name == existing.name;
        });
    if (found != latest.end()) {
      existing = *found;
    } else {
      existing.width = existing.height = 0;
      existing.right = existing.left;
      existing.bottom = existing.top;
    }
  }
}

void ScreenCapturerX11::OnFrame() {
  if (!display_) {
    LOG_ERROR("Display is not initialized");
    return;
  }

  const int monitor_index = monitor_index_.load();
  {
    std::lock_guard lock(display_info_mutex_);
    if (monitor_index < 0 ||
        monitor_index >= static_cast<int>(display_info_list_.size())) return;
    const auto& selected = display_info_list_[monitor_index];
    left_ = selected.left;
    top_ = selected.top;
    width_ = selected.width & ~1;
    height_ = selected.height & ~1;
  }

  if (width_ <= 1 || height_ <= 1) {
    LOG_ERROR("Invalid capture size: {}x{}", width_, height_);
    return;
  }

  XImage* image = nullptr;
  int x11_error = 0;
  {
    ScopedX11ErrorTrap trap(display_);
    image = XGetImage(display_, root_, left_, top_, width_, height_, AllPlanes,
                      ZPixmap);
    x11_error = trap.SyncAndGetError();
  }

  if (x11_error != 0 || !image) {
    if (image) {
      XDestroyImage(image);
    }
    ++capture_error_count_;
    if (capture_error_count_ == 1 || capture_error_count_ % 120 == 0) {
      LOG_WARN("X11 capture failed: x11_error={}, image={}, consecutive={}",
               x11_error, image ? "valid" : "null", capture_error_count_);
    }
    return;
  }
  capture_error_count_ = 0;

  // if enable show cursor, draw cursor
  if (show_cursor_) {
    Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    if (XQueryPointer(display_, root_, &root_return, &child_return, &root_x,
                      &root_y, &win_x, &win_y, &mask)) {
      if (root_x >= left_ && root_x < left_ + width_ && root_y >= top_ &&
          root_y < top_ + height_) {
        DrawCursor(image, root_x - left_, root_y - top_);
      }
    }
  }

  if (image->bits_per_pixel != 32 || image->bytes_per_line <= 0) {
    LOG_WARN(
        "Unsupported X11 image layout: bits_per_pixel={}, bytes_per_line={}",
        image->bits_per_pixel, image->bytes_per_line);
    XDestroyImage(image);
    return;
  }

  const uint8_t* src_argb = reinterpret_cast<const uint8_t*>(image->data);
  const int src_stride_argb = image->bytes_per_line;

  const size_t y_size =
      static_cast<size_t>(width_) * static_cast<size_t>(height_);
  const size_t uv_size = y_size / 2;
  if (y_plane_.size() != y_size) {
    y_plane_.resize(y_size);
  }
  if (uv_plane_.size() != uv_size) {
    uv_plane_.resize(uv_size);
  }

  const int convert_ret =
      use_abgr_to_nv12_
          ? libyuv::ABGRToNV12(src_argb, src_stride_argb, y_plane_.data(),
                               width_, uv_plane_.data(), width_, width_,
                               height_)
          : libyuv::ARGBToNV12(src_argb, src_stride_argb, y_plane_.data(),
                               width_, uv_plane_.data(), width_, width_,
                               height_);
  if (convert_ret != 0) {
    LOG_WARN("X11 {} failed: {}",
             use_abgr_to_nv12_ ? "ABGRToNV12" : "ARGBToNV12", convert_ret);
    XDestroyImage(image);
    return;
  }

  std::vector<uint8_t> nv12;
  nv12.reserve(y_plane_.size() + uv_plane_.size());
  nv12.insert(nv12.end(), y_plane_.begin(), y_plane_.end());
  nv12.insert(nv12.end(), uv_plane_.begin(), uv_plane_.end());

  if (callback_) {
    const std::string stream_id = MakeDisplayStreamId(monitor_index);
    callback_(nv12.data(), width_ * height_ * 3 / 2, width_, height_,
              stream_id.c_str(), nullptr);
  }

  XDestroyImage(image);
}

void ScreenCapturerX11::DrawCursor(XImage* image, int x, int y) {
  if (!display_ || !image) {
    return;
  }

  // check XFixes extension
  int event_base, error_base;
  if (!XFixesQueryExtension(display_, &event_base, &error_base)) {
    return;
  }

  XFixesCursorImage* cursor_image = XFixesGetCursorImage(display_);
  if (!cursor_image) {
    return;
  }

  int cursor_width = cursor_image->width;
  int cursor_height = cursor_image->height;

  int draw_x = x - cursor_image->xhot;
  int draw_y = y - cursor_image->yhot;

  // draw cursor on image
  for (int cy = 0; cy < cursor_height; ++cy) {
    for (int cx = 0; cx < cursor_width; ++cx) {
      int img_x = draw_x + cx;
      int img_y = draw_y + cy;

      if (img_x < 0 || img_x >= image->width || img_y < 0 ||
          img_y >= image->height) {
        continue;
      }

      unsigned long cursor_pixel = cursor_image->pixels[cy * cursor_width + cx];
      unsigned char a = (cursor_pixel >> 24) & 0xFF;

      // if alpha is 0, skip
      if (a == 0) {
        continue;
      }

      unsigned long img_pixel = XGetPixel(image, img_x, img_y);

      unsigned char img_r = (img_pixel >> 16) & 0xFF;
      unsigned char img_g = (img_pixel >> 8) & 0xFF;
      unsigned char img_b = img_pixel & 0xFF;

      unsigned char cursor_r = (cursor_pixel >> 16) & 0xFF;
      unsigned char cursor_g = (cursor_pixel >> 8) & 0xFF;
      unsigned char cursor_b = cursor_pixel & 0xFF;

      // alpha mix
      unsigned char final_r, final_g, final_b;
      if (a == 255) {
        // if alpha is 255, use cursor color
        final_r = cursor_r;
        final_g = cursor_g;
        final_b = cursor_b;
      } else {
        float alpha = a / 255.0f;
        float inv_alpha = 1.0f - alpha;
        final_r =
            static_cast<unsigned char>(cursor_r * alpha + img_r * inv_alpha);
        final_g =
            static_cast<unsigned char>(cursor_g * alpha + img_g * inv_alpha);
        final_b =
            static_cast<unsigned char>(cursor_b * alpha + img_b * inv_alpha);
      }

      // set pixel
      unsigned long new_pixel = (final_r << 16) | (final_g << 8) | final_b;
      XPutPixel(image, img_x, img_y, new_pixel);
    }
  }

  XFree(cursor_image);
}

bool ScreenCapturerX11::ProbeCapture() {
  if (!display_ || display_info_list_.empty()) {
    return false;
  }

  const auto& first_display = display_info_list_[0];
  XImage* probe_image = nullptr;
  int x11_error = 0;
  {
    ScopedX11ErrorTrap trap(display_);
    probe_image = XGetImage(display_, root_, first_display.left,
                            first_display.top, 1, 1, AllPlanes, ZPixmap);
    x11_error = trap.SyncAndGetError();
  }

  if (x11_error != 0 || !probe_image) {
    LOG_WARN("X11 probe XGetImage failed: x11_error={}, image={}", x11_error,
             probe_image ? "valid" : "null");
    return false;
  }

  const bool red_in_low_byte = (probe_image->red_mask & 0x000000FFu) != 0;
  const bool blue_in_low_byte = (probe_image->blue_mask & 0x000000FFu) != 0;
  use_abgr_to_nv12_ = red_in_low_byte && !blue_in_low_byte;

  XDestroyImage(probe_image);

  return true;
}
}  // namespace crossdesk
