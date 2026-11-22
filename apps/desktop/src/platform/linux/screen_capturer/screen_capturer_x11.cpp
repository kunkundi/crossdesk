#include "screen_capturer_x11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <thread>

#include <display_stream_id.h>
#include "libyuv.h"
#include "rd_log.h"
#include "x11_error_trap.h"

namespace crossdesk {
namespace {
void MarkOutputUnavailable(DisplayInfo& output) {
  output.width = output.height = 0;
  output.right = output.left;
  output.bottom = output.top;
}
}  // namespace

ScreenCapturerX11::ScreenCapturerX11()
    : wake_fd_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {}

ScreenCapturerX11::~ScreenCapturerX11() {
  Destroy();
  if (wake_fd_ >= 0) close(wake_fd_);
}

int ScreenCapturerX11::Init(const int fps, cb_desktop_data cb) {
  Destroy();
  if (!cb) return -1;
  if (wake_fd_ < 0) {
    LOG_ERROR("Cannot create X11 capture wake event");
    return -1;
  }

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
  displays_need_refresh_ = root_fallback_ = false;
  next_display_refresh_ = {};

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
  next_capture_error_log_ = {};
  displays_need_refresh_ = true;
  next_display_refresh_ = {};
  video_wanted_ = true;
  thread_ = std::thread([this] { CaptureLoop(); });
  return 0;
}

void ScreenCapturerX11::Wake() {
  // A pending event also covers later changes. Retain it until the worker
  // observes it, including when the change arrives just before poll().
  while (eventfd_write(wake_fd_, 1) < 0 && errno == EINTR) {}
}

void ScreenCapturerX11::CaptureLoop() {
  using Clock = std::chrono::steady_clock;
  using namespace std::chrono;
  const auto frame_interval = milliseconds(std::max(1, 1000 / std::max(1, fps_)));
  auto next_frame = Clock::now();
  pollfd fds[] = {{wake_fd_, POLLIN, 0}, {ConnectionNumber(display_), POLLIN, 0}};

  while (running_) {
    if (HandleDisplayEvents()) next_frame = Clock::now();
    const auto now = Clock::now();
    const bool capture = !paused_ && video_wanted_;
    if (capture && now >= next_frame) {
      OnFrame();
      if (capture_error_count_ > 0) {
        const int shift = std::min(capture_error_count_ - 1, 4);
        next_frame = Clock::now() + milliseconds(std::min(1000, 100 << shift));
      } else {
        next_frame = now + frame_interval;
      }
    }

    auto deadline = capture ? next_frame : Clock::time_point::max();
    if (displays_need_refresh_ || capture_error_count_ > 0)
      deadline = std::min(deadline, next_display_refresh_);
    // A synchronous X request may already have pulled a RandR event into
    // Xlib's queue, in which case the connection fd need not be readable.
    if (XEventsQueued(display_, QueuedAlready)) continue;
    int timeout = -1;
    if (deadline != Clock::time_point::max())
      timeout = static_cast<int>(std::max(milliseconds::zero(),
          ceil<milliseconds>(deadline - Clock::now())).count());
    if (poll(fds, 2, timeout) < 0) {
      if (errno == EINTR) continue;
      LOG_ERROR("X11 capture poll failed: errno={}", errno);
      break;
    }
    if (fds[0].revents & POLLIN) {
      eventfd_t value;
      while (eventfd_read(wake_fd_, &value) < 0 && errno == EINTR) {}
      next_frame = Clock::now();
    }
    if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
      LOG_ERROR("X11 capture connection closed");
      break;
    }
  }
}

void ScreenCapturerX11::SetVideoWanted(bool wanted) {
  if (video_wanted_.exchange(wanted) != wanted) {
    LOG_INFO("X11 capture video demand: {}", wanted);
    Wake();
  }
}

int ScreenCapturerX11::Stop() {
  if (!running_) return 0;
  running_ = false;
  Wake();
  if (thread_.joinable()) thread_.join();
  return 0;
}

int ScreenCapturerX11::Pause(int monitor_index) {
  if (!paused_.exchange(true)) Wake();
  return 0;
}

int ScreenCapturerX11::Resume(int monitor_index) {
  if (paused_.exchange(false)) Wake();
  return 0;
}

int ScreenCapturerX11::SwitchTo(int monitor_index) {
  std::lock_guard lock(display_info_mutex_);
  if (monitor_index < 0 ||
      monitor_index >= static_cast<int>(display_info_list_.size())) return -1;
  if (monitor_index_.exchange(monitor_index) != monitor_index) Wake();
  return 0;
}

int ScreenCapturerX11::ResetToInitialMonitor() {
  return SwitchTo(initial_monitor_index_);
}
std::vector<DisplayInfo> ScreenCapturerX11::GetDisplayInfoList() {
  std::lock_guard lock(display_info_mutex_);
  return display_info_list_;
}

bool ScreenCapturerX11::HandleDisplayEvents() {
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
  // A driver can announce a mode transition before its new CRTC is usable,
  // without sending another event when it settles. Retry even while paused.
  if (changed || ((displays_need_refresh_ || capture_error_count_ > 0) &&
                  std::chrono::steady_clock::now() >= next_display_refresh_)) {
    RefreshDisplays();
    if (capture_error_count_ > 0) {
      std::lock_guard lock(display_info_mutex_);
      const int index = monitor_index_;
      if (index >= 0 && index < static_cast<int>(display_info_list_.size())) {
        const auto& selected = display_info_list_[index];
        // Only a newly usable rectangle may bypass a failed frame's deadline.
        return selected.width > 1 && selected.height > 1 &&
               (selected.left != left_ || selected.top != top_ ||
                (selected.width & ~1) != width_ || (selected.height & ~1) != height_);
      }
    }
  }
  return false;
}

void ScreenCapturerX11::RefreshDisplays() {
  next_display_refresh_ =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  displays_need_refresh_ = true;
  ScopedX11ErrorTrap trap(display_);
  XWindowAttributes attr{};
  if (!XGetWindowAttributes(display_, root_, &attr)) return;
  if (attr.width <= 1 || attr.height <= 1) {
    // Keep the slots for recovery, but their old rectangles cannot be read.
    std::lock_guard lock(display_info_mutex_);
    for (auto& output : display_info_list_) MarkOutputUnavailable(output);
    return;
  }
  std::vector<DisplayInfo> latest;
  auto* resources = randr_event_base_ >= 0
                        ? XRRGetScreenResources(display_, root_)
                        : nullptr;
  // A failed query is not evidence that every output was disconnected.
  if (randr_event_base_ >= 0 && !resources) return;
  bool query_failed = false;
  if (resources) {
    const auto primary = XRRGetOutputPrimary(display_, root_);
    for (int i = 0; i < resources->noutput; ++i) {
      const auto id = resources->outputs[i];
      auto* output = XRRGetOutputInfo(display_, resources, id);
      if (!output) {
        query_failed = true;
        continue;
      }
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
        } else {
          query_failed = true;
        }
      }
      XRRFreeOutputInfo(output);
    }
    XRRFreeScreenResources(resources);
  }
  if (trap.SyncAndGetError() || query_failed) return;
  displays_need_refresh_ = latest.empty();
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
  bool root_fallback = false;
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
      displays_need_refresh_ = true;
      if (display_info_list_.size() == 1 && latest.empty()) {
        // With no active outputs, X11 can still provide a virtual root
        // desktop. Preserve the output identity so waking it restores the
        // same stream slot and mouse geometry. Never substitute another
        // active screen for a missing screen in a multi-monitor session.
        existing.left = existing.top = 0;
        existing.right = existing.width = attr.width & ~1;
        existing.bottom = existing.height = attr.height & ~1;
        root_fallback = true;
      } else {
        MarkOutputUnavailable(existing);
      }
    }
  }
  if (root_fallback != root_fallback_) {
    LOG_INFO("X11 root desktop fallback {}: root={}x{}",
             root_fallback ? "enabled" : "disabled", attr.width, attr.height);
    root_fallback_ = root_fallback;
  }
}

void ScreenCapturerX11::CaptureFailed(const char* reason, int x11_error) {
  ++capture_error_count_;
  const auto now = std::chrono::steady_clock::now();
  if (capture_error_count_ == 1 || now >= next_capture_error_log_) {
    LOG_WARN("X11 capture unavailable: reason={}, size={}x{}, x11_error={}, "
             "consecutive={}; retrying display detection",
             reason, width_, height_, x11_error, capture_error_count_);
    next_capture_error_log_ = now + std::chrono::seconds(5);
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
    CaptureFailed("invalid display geometry");
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
    CaptureFailed("XGetImage failed", x11_error);
    return;
  }

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
    CaptureFailed("unsupported X11 image layout");
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
    CaptureFailed("NV12 conversion failed");
    XDestroyImage(image);
    return;
  }
  if (capture_error_count_ > 0) {
    LOG_INFO("X11 capture recovered: size={}x{}, failed_frames={}",
             width_, height_, capture_error_count_);
    capture_error_count_ = 0;
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
