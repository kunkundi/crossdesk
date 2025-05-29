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

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOSurface/IOSurface.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <mutex>
#include <vector>
#include "display_info.h"
#include "rd_log.h"

static const int kFullDesktopScreenId = -1;
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
  int Init(const int fps, cb_desktop_data cb) override;

  int Start() override;

  int SwitchTo(int monitor_index) override;

  int Destroy() override;

  int Stop() override;

  int Pause(int monitor_index) override { return 0; }

  int Resume(int monitor_index) override { return 0; }

  std::vector<DisplayInfo> GetDisplayInfoList() override { return display_info_list_; }

 private:
  std::vector<DisplayInfo> display_info_list_;
  std::map<int, CGDirectDisplayID> display_id_map_;
  std::map<CGDirectDisplayID, int> display_id_map_reverse_;
  std::map<CGDirectDisplayID, std::string> display_id_name_map_;
  unsigned char *nv12_frame_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int fps_ = 30;

 public:
  // Called by SckHelper when shareable content is returned by ScreenCaptureKit. `content` will be
  // nil if an error occurred. May run on an arbitrary thread.
  void OnShareableContentCreated(SCShareableContent *content);
  // Called by SckHelper to notify of a newly captured frame. May run on an arbitrary thread.
  // void OnNewIOSurface(IOSurfaceRef io_surface, CFDictionaryRef attachment);
  void OnNewCVPixelBuffer(CVPixelBufferRef pixelBuffer, CFDictionaryRef attachment);

 private:
  // Called when starting the capturer or the configuration has changed (either from a
  // SwitchTo() call, or the screen-resolution has changed). This tells SCK to fetch new
  // shareable content, and the completion-handler will either start a new stream, or reconfigure
  // the existing stream. Runs on the caller's thread.
  void StartOrReconfigureCapturer();
  // Helper object to receive Objective-C callbacks from ScreenCaptureKit and call into this C++
  // object. The helper may outlive this C++ instance, if a completion-handler is passed to
  // ScreenCaptureKit APIs and the C++ object is deleted before the handler executes.
  SckHelper *__strong helper_;
  // Callback for returning captured frames, or errors, to the caller. Only used on the caller's
  // thread.
  cb_desktop_data _on_data = nullptr;
  // Signals that a permanent error occurred. This may be set on any thread, and is read by
  // CaptureFrame() which runs on the caller's thread.
  std::atomic<bool> permanent_error_ = false;
  // Guards some variables that may be accessed on different threads.
  std::mutex lock_;
  // Provides captured desktop frames.
  SCStream *__strong stream_;
  // Currently selected display, or 0 if the full desktop is selected. This capturer does not
  // support full-desktop capture, and will fall back to the first display.
  CGDirectDisplayID current_display_ = 0;
};

std::string GetDisplayName(CGDirectDisplayID display_id) {
  io_iterator_t iter;
  io_service_t serv = 0, matched_serv = 0;

  CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
  if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS) {
    return "";
  }

  while ((serv = IOIteratorNext(iter)) != 0) {
    CFDictionaryRef info = IODisplayCreateInfoDictionary(serv, kIODisplayOnlyPreferredName);
    if (info) {
      CFNumberRef vendorID = (CFNumberRef)CFDictionaryGetValue(info, CFSTR(kDisplayVendorID));
      CFNumberRef productID = (CFNumberRef)CFDictionaryGetValue(info, CFSTR(kDisplayProductID));
      uint32_t vID = 0, pID = 0;
      if (vendorID && productID && CFNumberGetValue(vendorID, kCFNumberIntType, &vID) &&
          CFNumberGetValue(productID, kCFNumberIntType, &pID) &&
          vID == CGDisplayVendorNumber(display_id) && pID == CGDisplayModelNumber(display_id)) {
        matched_serv = serv;
        CFRelease(info);
        break;
      }
      CFRelease(info);
    }
    IOObjectRelease(serv);
  }
  IOObjectRelease(iter);

  if (!matched_serv) return "";

  CFDictionaryRef display_info =
      IODisplayCreateInfoDictionary(matched_serv, kIODisplayOnlyPreferredName);
  IOObjectRelease(matched_serv);
  if (!display_info) return "";

  CFDictionaryRef product_name_dict =
      (CFDictionaryRef)CFDictionaryGetValue(display_info, CFSTR(kDisplayProductName));
  std::string result;
  if (product_name_dict) {
    CFIndex count = CFDictionaryGetCount(product_name_dict);
    if (count > 0) {
      std::vector<const void *> keys(count);
      std::vector<const void *> values(count);
      CFDictionaryGetKeysAndValues(product_name_dict, keys.data(), values.data());
      CFStringRef name_ref = (CFStringRef)values[0];
      if (name_ref) {
        CFIndex maxSize =
            CFStringGetMaximumSizeForEncoding(CFStringGetLength(name_ref), kCFStringEncodingUTF8) +
            1;
        std::vector<char> buffer(maxSize);
        if (CFStringGetCString(name_ref, buffer.data(), buffer.size(), kCFStringEncodingUTF8)) {
          result = buffer.data();
        }
      }
    }
  }
  CFRelease(display_info);
  return result;
}

ScreenCapturerSckImpl::ScreenCapturerSckImpl() {
  helper_ = [[SckHelper alloc] initWithCapturer:this];
}

ScreenCapturerSckImpl::~ScreenCapturerSckImpl() {
  display_info_list_.clear();
  display_id_map_.clear();
  display_id_map_reverse_.clear();
  display_id_name_map_.clear();

  if (nv12_frame_) {
    delete[] nv12_frame_;
    nv12_frame_ = nullptr;
  }

  [stream_ stopCaptureWithCompletionHandler:nil];
  [helper_ releaseCapturer];
}

int ScreenCapturerSckImpl::Init(const int fps, cb_desktop_data cb) {
  _on_data = cb;

  dispatch_semaphore_t sema = dispatch_semaphore_create(0);
  __block SCShareableContent *content = nil;

  [SCShareableContent
      getShareableContentWithCompletionHandler:^(SCShareableContent *result, NSError *error) {
        if (error) {
          NSLog(@"Failed to get shareable content: %@", error);
        } else {
          content = result;
        }
        dispatch_semaphore_signal(sema);
      }];
  dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

  if (!content || content.displays.count == 0) {
    LOG_ERROR("Failed to get display info")
    return 0;
  }

  CGDirectDisplayID displays[10];
  uint32_t count;
  CGGetActiveDisplayList(10, displays, &count);

  int unnamed_count = 1;
  for (SCDisplay *display in content.displays) {
    CGDirectDisplayID display_id = display.displayID;
    CGRect bounds = CGDisplayBounds(display_id);
    bool is_primary = CGDisplayIsMain(display_id);

    std::string name;
    name = GetDisplayName(display_id);

    if (name.empty()) {
      name = "Display " + std::to_string(unnamed_count++);
    }

    DisplayInfo info((void *)(uintptr_t)display_id, name, is_primary,
                     static_cast<int>(bounds.origin.x), static_cast<int>(bounds.origin.y),
                     static_cast<int>(bounds.origin.x + bounds.size.width),
                     static_cast<int>(bounds.origin.y + bounds.size.height));

    display_info_list_.push_back(info);
    display_id_map_[display_info_list_.size() - 1] = display_id;
    display_id_map_reverse_[display_id] = display_info_list_.size() - 1;
    display_id_name_map_[display_id] = name;
  }

  return 0;
}

int ScreenCapturerSckImpl::Start() {
  StartOrReconfigureCapturer();
  return 0;
}

int ScreenCapturerSckImpl::SwitchTo(int monitor_index) {
  if (stream_) {
    [stream_ stopCaptureWithCompletionHandler:^(NSError *error) {
      std::lock_guard<std::mutex> lock(lock_);
      stream_ = nil;
      current_display_ = display_id_map_[monitor_index];
      StartOrReconfigureCapturer();
    }];
  } else {
    current_display_ = display_id_map_[monitor_index];
    StartOrReconfigureCapturer();
  }
  return 0;
}

int ScreenCapturerSckImpl::Destroy() {
  std::lock_guard<std::mutex> lock(lock_);
  if (stream_) {
    LOG_INFO("Destroying stream");
    [stream_ stopCaptureWithCompletionHandler:nil];
    stream_ = nil;
  }
  current_display_ = 0;
  permanent_error_ = false;
  _on_data = nullptr;
  [helper_ releaseCapturer];
  helper_ = nil;

  return 0;
}

int ScreenCapturerSckImpl::Stop() {
  [stream_ stopCaptureWithCompletionHandler:nil];
  std::lock_guard<std::mutex> lock(lock_);
  if (stream_) {
    LOG_INFO("Stopping stream");
    [stream_ stopCaptureWithCompletionHandler:nil];
    stream_ = nil;
  }
  current_display_ = 0;

  return 0;
}

void ScreenCapturerSckImpl::OnShareableContentCreated(SCShareableContent *content) {
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
    std::lock_guard<std::mutex> lock(lock_);
    for (SCDisplay *display in content.displays) {
      if (current_display_ == display.displayID) {
        LOG_WARN("current display: {}, name: {}", current_display_,
                 display_id_name_map_[current_display_]);
        captured_display = display;
        break;
      }
    }
    if (!captured_display) {
      captured_display = content.displays.firstObject;
      current_display_ = captured_display.displayID;
    }
  }

  SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:captured_display
                                                    excludingWindows:@[]];
  SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
  config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  config.showsCursor = false;
  config.width = filter.contentRect.size.width * filter.pointPixelScale;
  config.height = filter.contentRect.size.height * filter.pointPixelScale;
  config.captureResolution = SCCaptureResolutionAutomatic;
  config.minimumFrameInterval = CMTimeMake(1, fps_);

  std::lock_guard<std::mutex> lock(lock_);

  if (stream_) {
    LOG_INFO("Updating stream configuration");
    [stream_ updateContentFilter:filter completionHandler:nil];
    [stream_ updateConfiguration:config completionHandler:nil];
  } else {
    stream_ = [[SCStream alloc] initWithFilter:filter configuration:config delegate:helper_];

    // TODO: crbug.com/327458809 - Choose an appropriate sampleHandlerQueue for
    // best performance.
    NSError *add_stream_output_error;
    dispatch_queue_t queue = dispatch_queue_create("ScreenCaptureKit.Queue", DISPATCH_QUEUE_SERIAL);
    bool add_stream_output_result = [stream_ addStreamOutput:helper_
                                                        type:SCStreamOutputTypeScreen
                                          sampleHandlerQueue:queue
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

void ScreenCapturerSckImpl::OnNewCVPixelBuffer(CVPixelBufferRef pixelBuffer,
                                               CFDictionaryRef attachment) {
  size_t width = CVPixelBufferGetWidth(pixelBuffer);
  size_t height = CVPixelBufferGetHeight(pixelBuffer);

  CVReturn status = CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
  if (status != kCVReturnSuccess) {
    LOG_ERROR("Failed to lock CVPixelBuffer base address: %d", status);
    return;
  }

  size_t required_size = width * height * 3 / 2;
  if (!nv12_frame_ || (width_ * height_ * 3 / 2 < required_size)) {
    delete[] nv12_frame_;
    nv12_frame_ = new unsigned char[required_size];
    width_ = width;
    height_ = height;
  }

  void *base_y = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
  size_t stride_y = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);

  void *base_uv = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
  size_t stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);

  unsigned char *dst_y = nv12_frame_;
  for (size_t row = 0; row < height; ++row) {
    memcpy(dst_y + row * width, static_cast<unsigned char *>(base_y) + row * stride_y, width);
  }

  unsigned char *dst_uv = nv12_frame_ + width * height;
  for (size_t row = 0; row < height / 2; ++row) {
    memcpy(dst_uv + row * width, static_cast<unsigned char *>(base_uv) + row * stride_uv, width);
  }

  _on_data(nv12_frame_, width * height * 3 / 2, width, height,
           display_id_name_map_[current_display_].c_str());

  CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

void ScreenCapturerSckImpl::StartOrReconfigureCapturer() {
  // The copy is needed to avoid capturing `this` in the Objective-C block. Accessing `helper_`
  // inside the block is equivalent to `this->helper_` and would crash (UAF) if `this` is
  // deleted before the block is executed.
  SckHelper *local_helper = helper_;
  auto handler = ^(SCShareableContent *content, NSError *error) {
    [local_helper onShareableContentCreated:content];
  };
  [SCShareableContent getShareableContentWithCompletionHandler:handler];
}

std::unique_ptr<ScreenCapturer> ScreenCapturerSck::CreateScreenCapturerSck() {
  return std::make_unique<ScreenCapturerSckImpl>();
}

@implementation SckHelper {
  // This lock is to prevent the capturer being destroyed while an instance
  // method is still running on another thread.
  std::mutex _capturer_lock;
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
  std::lock_guard<std::mutex> lock(_capturer_lock);
  if (_capturer) {
    _capturer->OnShareableContentCreated(content);
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

  CFRetain(pixelBuffer);

  CFArrayRef attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
  if (!attachmentsArray || CFArrayGetCount(attachmentsArray) == 0) {
    LOG_ERROR("Discarding frame with no attachments");
    CFRelease(pixelBuffer);
    return;
  }

  CFDictionaryRef attachment =
      static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachmentsArray, 0));

  std::lock_guard<std::mutex> lock(_capturer_lock);
  if (_capturer) {
    _capturer->OnNewCVPixelBuffer(pixelBuffer, attachment);
  }

  CFRelease(pixelBuffer);
}

- (void)releaseCapturer {
  std::lock_guard<std::mutex> lock(_capturer_lock);
  _capturer = nullptr;
}

@end