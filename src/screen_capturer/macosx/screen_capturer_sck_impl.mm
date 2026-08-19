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
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <vector>
#include "display_info.h"
#include "rd_log.h"

using namespace crossdesk;

class ScreenCapturerSckImpl;

static const int kFullDesktopScreenId = -1;

static std::string NSErrorToString(NSError *error) {
  if (!error) {
    return "";
  }

  const char *description = [error.localizedDescription UTF8String];
  return description ? description : "";
}

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

  int Start(bool show_cursor) override;

  int SwitchTo(int monitor_index) override;

  int Destroy() override;

  int Stop() override;

  int Pause(int monitor_index) override { return 0; }

  int Resume(int monitor_index) override { return 0; }

  std::vector<DisplayInfo> GetDisplayInfoList() override { return display_info_list_; }
  int ResetToInitialMonitor() override;

 private:
  std::vector<DisplayInfo> display_info_list_;
  std::map<int, CGDirectDisplayID> display_id_map_;
  std::map<CGDirectDisplayID, int> display_id_map_reverse_;
  std::map<CGDirectDisplayID, std::string> display_id_name_map_;
  unsigned char *nv12_frame_ = nullptr;
  size_t nv12_frame_size_ = 0;
  int width_ = 0;
  int height_ = 0;
  int fps_ = 60;
  bool show_cursor_ = false;

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
  SckHelper *__strong helper_ = nil;
  // Callback for returning captured frames, or errors, to the caller. Only used on the caller's
  // thread.
  cb_desktop_data _on_data = nullptr;
  // Signals that a permanent error occurred. This may be set on any thread, and is read by
  // CaptureFrame() which runs on the caller's thread.
  std::atomic<bool> permanent_error_ = false;
  // Guards some variables that may be accessed on different threads.
  std::mutex lock_;
  // Provides captured desktop frames.
  SCStream *__strong stream_ = nil;
  // Currently selected display, or 0 if the full desktop is selected. This capturer does not
  // support full-desktop capture, and will fall back to the first display.
  CGDirectDisplayID current_display_ = 0;
  int initial_monitor_index_ = 0;
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
  SckHelper *helper_to_release = nil;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (stream_) {
      [stream_ stopCaptureWithCompletionHandler:nil];
      stream_ = nil;
    }
    _on_data = nullptr;
    helper_to_release = helper_;
    helper_ = nil;
  }
  [helper_to_release releaseCapturer];

  display_info_list_.clear();
  display_id_map_.clear();
  display_id_map_reverse_.clear();
  display_id_name_map_.clear();

  if (nv12_frame_) {
    delete[] nv12_frame_;
    nv12_frame_ = nullptr;
    nv12_frame_size_ = 0;
  }
}

int ScreenCapturerSckImpl::Init(const int fps, cb_desktop_data cb) {
  if (!cb) {
    LOG_ERROR("Screen capturer callback is null");
    return -1;
  }

  _on_data = cb;
  fps_ = fps > 0 ? fps : 60;
  display_info_list_.clear();
  display_id_map_.clear();
  display_id_map_reverse_.clear();
  display_id_name_map_.clear();

  if (@available(macOS 10.15, *)) {
    bool has_permission = CGPreflightScreenCaptureAccess();
    if (!has_permission) {
      LOG_ERROR("Screen recording permission not granted");
      return -1;
    }
  }

  dispatch_semaphore_t sema = dispatch_semaphore_create(0);
  __block SCShareableContent *content = nil;
  __block NSError *capture_error = nil;

  [SCShareableContent
      getShareableContentWithCompletionHandler:^(SCShareableContent *result, NSError *error) {
        if (error) {
          capture_error = error;
          LOG_ERROR("Failed to get shareable content: {}", NSErrorToString(error));
        } else {
          content = result;
        }
        dispatch_semaphore_signal(sema);
      }];
  dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

  if (capture_error || !content || content.displays.count == 0) {
    LOG_ERROR("Failed to get display info, error: {}",
              NSErrorToString(capture_error));
    return -1;
  }

  CGDirectDisplayID displays[10];
  uint32_t count;
  CGGetActiveDisplayList(10, displays, &count);

  int unnamed_count = 1;
  for (SCDisplay *display in content.displays) {
    CGDirectDisplayID display_id = display.displayID;
    CGRect bounds = CGDisplayBounds(display_id);
    bool is_primary = CGDisplayIsMain(display_id);

    std::string name = GetDisplayName(display_id);

    if (name.empty()) {
      name = "Display" + std::to_string(unnamed_count++);
    }

    // clean display name, remove non-alphanumeric characters
    name.erase(
        std::remove_if(name.begin(), name.end(), [](unsigned char c) { return !std::isalnum(c); }),
        name.end());

    DisplayInfo info((void *)(uintptr_t)display_id, name, is_primary,
                     static_cast<int>(bounds.origin.x), static_cast<int>(bounds.origin.y),
                     static_cast<int>(bounds.origin.x + bounds.size.width),
                     static_cast<int>(bounds.origin.y + bounds.size.height));

    display_info_list_.push_back(info);
    display_id_map_[display_info_list_.size() - 1] = display_id;
    display_id_map_reverse_[display_id] = display_info_list_.size() - 1;
    display_id_name_map_[display_id] = name;
  }

  initial_monitor_index_ = 0;
  return 0;
}

int ScreenCapturerSckImpl::Start(bool show_cursor) {
  if (permanent_error_) {
    LOG_ERROR("Cannot start capturer: permanent error occurred");
    return -1;
  }

  if (display_info_list_.empty()) {
    LOG_ERROR("Cannot start capturer: display info not initialized");
    return -1;
  }

  show_cursor_ = show_cursor;
  StartOrReconfigureCapturer();
  return 0;
}

int ScreenCapturerSckImpl::SwitchTo(int monitor_index) {
  auto display_it = display_id_map_.find(monitor_index);
  if (display_it == display_id_map_.end()) {
    LOG_WARN("SwitchTo skipped, invalid monitor_index={}, displays={}",
             monitor_index, display_id_map_.size());
    return -1;
  }

  const CGDirectDisplayID target_display = display_it->second;
  {
    std::lock_guard<std::mutex> lock(lock_);
    current_display_ = target_display;
  }
  StartOrReconfigureCapturer();
  return 0;
}

int ScreenCapturerSckImpl::ResetToInitialMonitor() {
  const int target = initial_monitor_index_;
  if (display_info_list_.empty()) return -1;
  auto display_it = display_id_map_.find(target);
  if (display_it == display_id_map_.end()) {
    LOG_WARN("ResetToInitialMonitor skipped, invalid monitor_index={}", target);
    return -1;
  }

  const CGDirectDisplayID target_display = display_it->second;
  bool should_reconfigure = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (current_display_ == target_display) return 0;
    current_display_ = target_display;
    should_reconfigure = stream_ != nil;
  }

  // Resetting session state must not create a capture stream. Preserve the
  // selected monitor for the next Start(), and only reconfigure an active one.
  if (should_reconfigure) {
    StartOrReconfigureCapturer();
  }
  return 0;
}

int ScreenCapturerSckImpl::Destroy() {
  SckHelper *helper_to_release = nil;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (stream_) {
      LOG_INFO("Destroying stream");
      [stream_ stopCaptureWithCompletionHandler:nil];
      stream_ = nil;
    }
    current_display_ = 0;
    permanent_error_ = false;
    _on_data = nullptr;
    helper_to_release = helper_;
    helper_ = nil;
  }

  [helper_to_release releaseCapturer];

  return 0;
}

int ScreenCapturerSckImpl::Stop() {
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

  if (!content.displays || content.displays.count == 0) {
    LOG_ERROR("getShareableContent returned no displays");
    permanent_error_ = true;
    return;
  }

  SCDisplay *captured_display = nil;
  {
    std::lock_guard<std::mutex> lock(lock_);
    for (SCDisplay *display in content.displays) {
      if (current_display_ != 0 && current_display_ == display.displayID) {
        LOG_WARN("current display: {}, name: {}", current_display_,
                 display_id_name_map_[current_display_]);
        captured_display = display;
        break;
      }
    }
    if (!captured_display) {
      captured_display = content.displays.firstObject;
      if (captured_display) {
        current_display_ = captured_display.displayID;
      }
    }
  }

  if (!captured_display) {
    LOG_ERROR("Failed to find valid display");
    permanent_error_ = true;
    return;
  }

  SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:captured_display
                                                    excludingWindows:@[]];
  if (!filter) {
    LOG_ERROR("Failed to create SCContentFilter");
    permanent_error_ = true;
    return;
  }

  SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
  if (!config) {
    LOG_ERROR("Failed to create SCStreamConfiguration");
    permanent_error_ = true;
    return;
  }

  config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  config.showsCursor = show_cursor_;
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
    NSError *add_stream_output_error = nil;
    dispatch_queue_t queue = dispatch_queue_create("ScreenCaptureKit.Queue", DISPATCH_QUEUE_SERIAL);
    bool add_stream_output_result = [stream_ addStreamOutput:helper_
                                                        type:SCStreamOutputTypeScreen
                                          sampleHandlerQueue:queue
                                                       error:&add_stream_output_error];

    if (!add_stream_output_result) {
      stream_ = nil;
      LOG_ERROR("addStreamOutput failed: {}", NSErrorToString(add_stream_output_error));
      permanent_error_ = true;
      return;
    }

    auto handler = ^(NSError *error) {
      if (error) {
        // It should be safe to access `this` here, because the C++ destructor
        // calls stopCaptureWithCompletionHandler on the stream, which cancels
        // this handler.
        permanent_error_ = true;
        LOG_ERROR("startCaptureWithCompletionHandler failed: {}", NSErrorToString(error));
      } else {
        LOG_INFO("Capture started");
      }
    };

    [stream_ startCaptureWithCompletionHandler:handler];
  }
}

void ScreenCapturerSckImpl::OnNewCVPixelBuffer(CVPixelBufferRef pixelBuffer,
                                               CFDictionaryRef attachment) {
  (void)attachment;
  if (!pixelBuffer) {
    return;
  }

  size_t width = CVPixelBufferGetWidth(pixelBuffer);
  size_t height = CVPixelBufferGetHeight(pixelBuffer);
  if (width == 0 || height == 0 || CVPixelBufferGetPlaneCount(pixelBuffer) < 2) {
    LOG_ERROR("Invalid CVPixelBuffer: width={}, height={}, planes={}", width, height,
              CVPixelBufferGetPlaneCount(pixelBuffer));
    return;
  }

  CVReturn status = CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
  if (status != kCVReturnSuccess) {
    LOG_ERROR("Failed to lock CVPixelBuffer base address: %d", status);
    return;
  }

  size_t required_size = width * height * 3 / 2;
  if (required_size > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    LOG_ERROR("Captured frame is too large: {} bytes", required_size);
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    return;
  }

  std::lock_guard<std::mutex> lock(lock_);
  if (!_on_data) {
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    return;
  }

  if (!nv12_frame_ || nv12_frame_size_ < required_size) {
    delete[] nv12_frame_;
    nv12_frame_ = new unsigned char[required_size];
    nv12_frame_size_ = required_size;
  }
  width_ = static_cast<int>(width);
  height_ = static_cast<int>(height);

  void *base_y = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
  size_t stride_y = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);

  void *base_uv = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
  size_t stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
  if (!base_y || !base_uv || stride_y < width || stride_uv < width) {
    LOG_ERROR("Invalid CVPixelBuffer planes: base_y={}, base_uv={}, stride_y={}, stride_uv={}",
              base_y != nullptr, base_uv != nullptr, stride_y, stride_uv);
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    return;
  }

  unsigned char *dst_y = nv12_frame_;
  for (size_t row = 0; row < height; ++row) {
    memcpy(dst_y + row * width, static_cast<unsigned char *>(base_y) + row * stride_y, width);
  }

  unsigned char *dst_uv = nv12_frame_ + width * height;
  for (size_t row = 0; row < height / 2; ++row) {
    memcpy(dst_uv + row * width, static_cast<unsigned char *>(base_uv) + row * stride_uv, width);
  }

  _on_data(nv12_frame_, static_cast<int>(required_size), static_cast<int>(width),
           static_cast<int>(height),
           display_id_name_map_[current_display_].c_str());

  CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

void ScreenCapturerSckImpl::StartOrReconfigureCapturer() {
  if (permanent_error_) {
    LOG_ERROR("Cannot reconfigure capturer: permanent error occurred");
    return;
  }

  if (@available(macOS 10.15, *)) {
    bool has_permission = CGPreflightScreenCaptureAccess();
    if (!has_permission) {
      LOG_ERROR("Screen recording permission not granted");
      permanent_error_ = true;
      return;
    }
  }

  SckHelper *local_helper = helper_;
  if (!local_helper) {
    LOG_ERROR("Cannot reconfigure capturer: helper is null");
    return;
  }

  auto handler = ^(SCShareableContent *content, NSError *error) {
    if (error) {
      LOG_ERROR("getShareableContent failed: {}", NSErrorToString(error));
      [local_helper onShareableContentCreated:nil];
      return;
    }
    [local_helper onShareableContentCreated:content];
  };
  [SCShareableContent getShareableContentWithCompletionHandler:handler];
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

std::unique_ptr<ScreenCapturer> ScreenCapturerSck::CreateScreenCapturerSck() {
  return std::make_unique<ScreenCapturerSckImpl>();
}
