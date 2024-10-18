/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "screen_capturer_sck.h"

#include "rd_log.h"
#include <CoreGraphics/CoreGraphics.h>
#include <IOSurface/IOSurface.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <mutex>

class ScreenCapturerSckImpl;

// The ScreenCaptureKit API was available in macOS 12.3, but full-screen capture
// was reported to be broken before macOS 13 - see http://crbug.com/40234870.
// Also, the `SCContentFilter` fields `contentRect` and `pointPixelScale` were
// introduced in macOS 14.
API_AVAILABLE(macos(14.0))
@interface SckHelper : NSObject <SCStreamDelegate, SCStreamOutput>

- (instancetype)initWithCapturer:(ScreenCapturerSckImpl *)capturer;

- (void)onShareableContentCreated:(SCShareableContent *)content;

// Called just before the capturer is destroyed. This avoids a dangling pointer,
// and prevents any new calls into a deleted capturer. If any method-call on the
// capturer is currently running on a different thread, this blocks until it
// completes.
- (void)releaseCapturer;
@end

class API_AVAILABLE(macos(14.0)) ScreenCapturerSckImpl : public ScreenCapturer {
public:
  explicit ScreenCapturerSckImpl();

  ScreenCapturerSckImpl(const ScreenCapturerSckImpl &) = delete;
  ScreenCapturerSckImpl &operator=(const ScreenCapturerSckImpl &) = delete;
  ~ScreenCapturerSckImpl();

public:
  int Init(const int fps, cb_desktop_data cb);
  void OnReceiveContent(SCShareableContent *content);
  void OnNewIOSurface(IOSurfaceRef io_surface, CFDictionaryRef attachment);

  virtual int Destroy() { return 0; }

  virtual int Start() { return 0; }

  virtual int Stop() { return 0; }

private:
  SckHelper *__strong helper_;
  SCStream *__strong stream_;

  cb_desktop_data _on_data;
  unsigned char *nv12_frame_ = nullptr;
  bool permanent_error_ = false;
  CGDirectDisplayID current_display_ = -1;
  std::mutex mtx_;
};

@implementation SckHelper {
  // This lock is to prevent the capturer being destroyed while an instance
  // method is still running on another thread.
  std::mutex helper_mtx_;
  ScreenCapturerSckImpl *_capturer;
}

- (instancetype)initWithCapturer:(ScreenCapturerSckImpl *)capturer {
  self = [super init];
  if (self) {
    _capturer = capturer;
  }
  return self;
}

- (void)onShareableContentCreated:(SCShareableContent *)content {
  std::lock_guard<std::mutex> lock(helper_mtx_);
  if (_capturer) {
    _capturer->OnReceiveContent(content);
  } else {
    LOG_ERROR("Invalid capturer");
  }
}

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
  CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pixelBuffer) {
    return;
  }

  IOSurfaceRef ioSurface = CVPixelBufferGetIOSurface(pixelBuffer);
  if (!ioSurface) {
    return;
  }

  CFArrayRef attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(
      sampleBuffer, /*createIfNecessary=*/false);
  if (!attachmentsArray || CFArrayGetCount(attachmentsArray) <= 0) {
    LOG_ERROR("Discarding frame with no attachments");
    return;
  }

  CFDictionaryRef attachment =
      static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachmentsArray, 0));

  std::lock_guard<std::mutex> lock(helper_mtx_);
  if (_capturer) {
    _capturer->OnNewIOSurface(ioSurface, attachment);
  }
}

- (void)releaseCapturer {
  std::lock_guard<std::mutex> lock(helper_mtx_);
  _capturer = nullptr;
}

@end

ScreenCapturerSckImpl::ScreenCapturerSckImpl() {
  helper_ = [[SckHelper alloc] initWithCapturer:this];
}

ScreenCapturerSckImpl::~ScreenCapturerSckImpl() {
  [stream_ stopCaptureWithCompletionHandler:nil];
  [helper_ releaseCapturer];
}

int ScreenCapturerSckImpl::Init(const int fps, cb_desktop_data cb) {
  _on_data = cb;

  SckHelper *local_helper = helper_;
  auto handler = ^(SCShareableContent *content, NSError *error) {
    [local_helper onShareableContentCreated:content];
  };

  [SCShareableContent getShareableContentWithCompletionHandler:handler];

  return 0;
}

void ScreenCapturerSckImpl::OnReceiveContent(SCShareableContent *content) {
  if (!content) {
    LOG_ERROR("getShareableContent failed");
    permanent_error_ = true;
    return;
  }

  if (!content.displays.count) {
    LOG_ERROR("getShareableContent returned no displays");
    permanent_error_ = true;
    return;
  }

  SCDisplay *captured_display;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    for (SCDisplay *display in content.displays) {
      if (current_display_ == display.displayID) {
        captured_display = display;
        break;
      }
    }
    if (!captured_display) {
      if (-1 == current_display_) {
        LOG_ERROR("Full screen capture is not supported, falling back to first "
                  "display");
      } else {
        LOG_ERROR("Display [{}] not found, falling back to first display",
                  current_display_);
      }
      captured_display = content.displays.firstObject;
    }
  }

  SCContentFilter *filter =
      [[SCContentFilter alloc] initWithDisplay:captured_display
                              excludingWindows:@[]];
  SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
  config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  config.showsCursor = false;
  config.width = filter.contentRect.size.width * filter.pointPixelScale;
  config.height = filter.contentRect.size.height * filter.pointPixelScale;
  config.captureResolution = SCCaptureResolutionNominal;

  std::lock_guard<std::mutex> lock(mtx_);

  if (stream_) {
    LOG_INFO("Updating stream configuration");
    [stream_ updateContentFilter:filter completionHandler:nil];
    [stream_ updateConfiguration:config completionHandler:nil];
  } else {
    stream_ = [[SCStream alloc] initWithFilter:filter
                                 configuration:config
                                      delegate:helper_];

    // TODO: crbug.com/327458809 - Choose an appropriate sampleHandlerQueue for
    // best performance.
    NSError *add_stream_output_error;
    bool add_stream_output_result =
        [stream_ addStreamOutput:helper_
                            type:SCStreamOutputTypeScreen
              sampleHandlerQueue:nil
                           error:&add_stream_output_error];
    if (!add_stream_output_result) {
      stream_ = nil;
      LOG_ERROR("addStreamOutput failed");
      permanent_error_ = true;
      return;
    }

    auto handler = ^(NSError *error) {
      if (error) {
        // It should be safe to access `this` here, because the C++ destructor
        // calls stopCaptureWithCompletionHandler on the stream, which cancels
        // this handler.
        permanent_error_ = true;
        LOG_ERROR("startCaptureWithCompletionHandler failed");
      } else {
        LOG_INFO("Capture started");
      }
    };

    [stream_ startCaptureWithCompletionHandler:handler];
  }
}

void ScreenCapturerSckImpl::OnNewIOSurface(IOSurfaceRef io_surface,
                                           CFDictionaryRef attachment) {
  size_t width = IOSurfaceGetWidth(io_surface);
  size_t height = IOSurfaceGetHeight(io_surface);

  uint32_t aseed;
  IOSurfaceLock(io_surface, kIOSurfaceLockReadOnly, &aseed);

  nv12_frame_ =
      static_cast<unsigned char *>(IOSurfaceGetBaseAddress(io_surface));

  _on_data(nv12_frame_, width * height * 3 / 2, width, height);

  IOSurfaceUnlock(io_surface, kIOSurfaceLockReadOnly, &aseed);
}

std::unique_ptr<ScreenCapturer> ScreenCapturerSck::CreateScreenCapturerSck() {
  return std::make_unique<ScreenCapturerSckImpl>();
}