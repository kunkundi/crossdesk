#include <IOSurface/IOSurface.h>

#include <utility>

#include "rd_log.h"
#include "screen_capturer_cgd.h"

ScreenCapturerCg::ScreenCapturerCg() {}

ScreenCapturerCg::~ScreenCapturerCg() {}

int ScreenCapturerCg::Init(const int fps, cb_desktop_data cb) {
  if (cb) {
    _on_data = cb;
  }

  size_t pixel_width = 1280;
  size_t pixel_height = 720;
  CGDirectDisplayID display_id = 0;

  CGDisplayStreamFrameAvailableHandler handler =
      ^(CGDisplayStreamFrameStatus status, uint64_t display_time,
        IOSurfaceRef frame_surface, CGDisplayStreamUpdateRef updateRef) {
        if (status == kCGDisplayStreamFrameStatusStopped) return;
        // Only pay attention to frame updates.
        if (status != kCGDisplayStreamFrameStatusFrameComplete) return;

        // size_t count = 0;
        // const CGRect* rects = CGDisplayStreamUpdateGetRects(
        //     updateRef, kCGDisplayStreamUpdateDirtyRects, &count);

        // 获取帧数据
        void* frameData = IOSurfaceGetBaseAddressOfPlane(frame_surface, 0);
        size_t width = IOSurfaceGetWidthOfPlane(frame_surface, 0);
        size_t height = IOSurfaceGetHeightOfPlane(frame_surface, 0);
      };

  CFDictionaryRef properties_dictionary = CFDictionaryCreate(
      kCFAllocatorDefault, (const void*[]){kCGDisplayStreamShowCursor},
      (const void*[]){kCFBooleanFalse}, 1, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  CGDisplayStreamRef display_stream =
      CGDisplayStreamCreate(display_id, pixel_width, pixel_height, 'BGRA',
                            properties_dictionary, handler);

  if (display_stream) {
    CGError error = CGDisplayStreamStart(display_stream);
    if (error != kCGErrorSuccess) return -1;

    CFRunLoopSourceRef source = CGDisplayStreamGetRunLoopSource(display_stream);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
    display_streams_.push_back(display_stream);
  }

  CFRelease(properties_dictionary);

  return 0;
}

int ScreenCapturerCg::Destroy() {
  running_ = false;
  return 0;
}

int ScreenCapturerCg::Start() {
  if (_running) {
    return 0;
  }

  running_ = true;
  capture_thread_ = std::thread([this]() {
    while (running_) {
      CFRunLoopRun();
    }
  });

  return 0;
}

int ScreenCapturerCg::Stop() {
  running_ = false;
  return 0;
}

int ScreenCapturerCg::Pause() { return 0; }

int ScreenCapturerCg::Resume() { return 0; }

void ScreenCapturerCg::OnFrame() {}

void ScreenCapturerCg::CleanUp() {}

//

void ScreenCapturerCg::UnregisterRefreshAndMoveHandlers() {
  for (CGDisplayStreamRef stream : display_streams_) {
    CFRunLoopSourceRef source = CGDisplayStreamGetRunLoopSource(stream);
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
    CGDisplayStreamStop(stream);
    CFRelease(stream);
  }
  display_streams_.clear();
}
