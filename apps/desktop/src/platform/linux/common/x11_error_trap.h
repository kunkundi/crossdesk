/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _X11_ERROR_TRAP_H_
#define _X11_ERROR_TRAP_H_

#include <X11/Xlib.h>

#include <atomic>
#include <mutex>

namespace crossdesk {

// Xlib's error handler is process-wide. Capture and RandR transactions must
// share this lock so neither restores a handler while the other is using it.
inline std::mutex x11_error_handler_mutex;
inline std::atomic<int> x11_last_error_code{0};

class ScopedX11ErrorTrap {
 public:
  explicit ScopedX11ErrorTrap(Display* display)
      : display_(display), lock_(x11_error_handler_mutex) {
    x11_last_error_code.store(0);
    previous_handler_ = XSetErrorHandler([](Display*, XErrorEvent* event) {
      x11_last_error_code.store(event ? event->error_code : -1);
      return 0;
    });
  }

  ~ScopedX11ErrorTrap() {
    XSync(display_, False);
    XSetErrorHandler(previous_handler_);
  }

  int SyncAndGetError() const {
    XSync(display_, False);
    return x11_last_error_code.load();
  }

 private:
  Display* display_;
  std::unique_lock<std::mutex> lock_;
  XErrorHandler previous_handler_;
};

}  // namespace crossdesk

#endif