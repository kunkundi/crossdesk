#include "screen_capturer_x11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

#include <chrono>
#include <thread>

#include "libyuv.h"
#include "rd_log.h"

namespace crossdesk {

ScreenCapturerX11::ScreenCapturerX11() {}

ScreenCapturerX11::~ScreenCapturerX11() { Destroy(); }

int ScreenCapturerX11::Init(const int fps, cb_desktop_data cb) {
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    LOG_ERROR("Cannot connect to X server");
    return -1;
  }

  root_ = DefaultRootWindow(display_);
  screen_res_ = XRRGetScreenResources(display_, root_);
  if (!screen_res_) {
    LOG_ERROR("Failed to get screen resources");
    XCloseDisplay(display_);
    return 1;
  }

  for (int i = 0; i < screen_res_->noutput; ++i) {
    RROutput output = screen_res_->outputs[i];
    XRROutputInfo* output_info =
        XRRGetOutputInfo(display_, screen_res_, output);

    if (output_info->connection == RR_Connected && output_info->crtc != 0) {
      XRRCrtcInfo* crtc_info =
          XRRGetCrtcInfo(display_, screen_res_, output_info->crtc);

      std::string name(output_info->name);

      if (name.empty()) {
        name = "Display" + std::to_string(i + 1);
      }

      // clean display name, remove non-alphanumeric characters
      name.erase(
          std::remove_if(name.begin(), name.end(),
                         [](unsigned char c) { return !std::isalnum(c); }),
          name.end());

      display_info_list_.push_back(DisplayInfo(
          (void*)display_, name, true, crtc_info->x, crtc_info->y,
          crtc_info->x + crtc_info->width, crtc_info->y + crtc_info->height));

      XRRFreeCrtcInfo(crtc_info);
    }

    if (output_info) {
      XRRFreeOutputInfo(output_info);
    }
  }

  XWindowAttributes attr;
  XGetWindowAttributes(display_, root_, &attr);

  width_ = attr.width;
  height_ = attr.height;

  if (width_ % 2 != 0 || height_ % 2 != 0) {
    LOG_ERROR("Width and height must be even numbers");
    return -2;
  }

  fps_ = fps;
  callback_ = cb;

  y_plane_.resize(width_ * height_);
  uv_plane_.resize((width_ / 2) * (height_ / 2) * 2);

  return 0;
}

int ScreenCapturerX11::Destroy() {
  Stop();

  y_plane_.clear();
  uv_plane_.clear();

  if (screen_res_) {
    XRRFreeScreenResources(screen_res_);
    screen_res_ = nullptr;
  }

  if (display_) {
    XCloseDisplay(display_);
    display_ = nullptr;
  }
  return 0;
}

int ScreenCapturerX11::Start(bool show_cursor) {
  if (running_) return 0;
  show_cursor_ = show_cursor;
  running_ = true;
  paused_ = false;
  thread_ = std::thread([this]() {
    while (running_) {
      if (!paused_) OnFrame();
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
  monitor_index_ = monitor_index;
  return 0;
}

std::vector<DisplayInfo> ScreenCapturerX11::GetDisplayInfoList() {
  return display_info_list_;
}

void ScreenCapturerX11::OnFrame() {
  if (!display_) {
    LOG_ERROR("Display is not initialized");
    return;
  }

  if (monitor_index_ < 0 || monitor_index_ >= display_info_list_.size()) {
    LOG_ERROR("Invalid monitor index: {}", monitor_index_.load());
    return;
  }

  left_ = display_info_list_[monitor_index_].left;
  top_ = display_info_list_[monitor_index_].top;
  width_ = display_info_list_[monitor_index_].width;
  height_ = display_info_list_[monitor_index_].height;

  XImage* image = XGetImage(display_, root_, left_, top_, width_, height_,
                            AllPlanes, ZPixmap);
  if (!image) return;

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

  bool needs_copy = image->bytes_per_line != width_ * 4;
  std::vector<uint8_t> argb_buf;
  uint8_t* src_argb = nullptr;

  if (needs_copy) {
    argb_buf.resize(width_ * height_ * 4);
    for (int y = 0; y < height_; ++y) {
      memcpy(&argb_buf[y * width_ * 4], image->data + y * image->bytes_per_line,
             width_ * 4);
    }
    src_argb = argb_buf.data();
  } else {
    src_argb = reinterpret_cast<uint8_t*>(image->data);
  }

  libyuv::ARGBToNV12(src_argb, width_ * 4, y_plane_.data(), width_,
                     uv_plane_.data(), width_, width_, height_);

  std::vector<uint8_t> nv12;
  nv12.reserve(y_plane_.size() + uv_plane_.size());
  nv12.insert(nv12.end(), y_plane_.begin(), y_plane_.end());
  nv12.insert(nv12.end(), uv_plane_.begin(), uv_plane_.end());

  if (callback_) {
    callback_(nv12.data(), width_ * height_ * 3 / 2, width_, height_,
              display_info_list_[monitor_index_].name.c_str());
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
}  // namespace crossdesk