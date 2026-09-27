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

#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOSurface/IOSurface.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <vector>
#include "display_info.h"
#include <display_stream_id.h>
#include "rd_log.h"
#include "privacy_controller.h"
#include "../privacy/privacy_capture_state.h"
#include <algorithm>

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

static void RetainCapturedPixelBuffer(void *owner) {
  if (owner) {
    CVPixelBufferRetain(static_cast<CVPixelBufferRef>(owner));
  }
}

static void ReleaseCapturedPixelBuffer(void *owner) {
  if (owner) {
    CVPixelBufferRelease(static_cast<CVPixelBufferRef>(owner));
  }
}

static int CopyCapturedPixelBufferToNv12(void *owner, uint8_t *destination,
                                         size_t destination_size) {
  auto pixel_buffer = static_cast<CVPixelBufferRef>(owner);
  if (!pixel_buffer || !destination ||
      !CVPixelBufferIsPlanar(pixel_buffer) ||
      CVPixelBufferGetPlaneCount(pixel_buffer) < 2) {
    return -1;
  }
  const OSType pixel_format = CVPixelBufferGetPixelFormatType(pixel_buffer);
  if (pixel_format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange &&
      pixel_format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
    return -1;
  }

  const size_t width = CVPixelBufferGetWidth(pixel_buffer);
  const size_t height = CVPixelBufferGetHeight(pixel_buffer);
  const size_t required_size = width * height * 3U / 2U;
  if (destination_size < required_size ||
      CVPixelBufferLockBaseAddress(pixel_buffer,
                                   kCVPixelBufferLock_ReadOnly) !=
          kCVReturnSuccess) {
    return -1;
  }

  const auto *y_plane = static_cast<const uint8_t *>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
  const auto *uv_plane = static_cast<const uint8_t *>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1));
  const size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
  const size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);
  int result = 0;
  if (!y_plane || !uv_plane || y_stride < width || uv_stride < width) {
    result = -1;
  } else {
    for (size_t row = 0; row < height; ++row) {
      std::memcpy(destination + row * width, y_plane + row * y_stride, width);
    }
    uint8_t *uv_destination = destination + width * height;
    for (size_t row = 0; row < height / 2U; ++row) {
      std::memcpy(uv_destination + row * width,
                  uv_plane + row * uv_stride, width);
    }
  }
  CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
  return result;
}

// The ScreenCaptureKit API was available in macOS 12.3, but full-screen capture
// was reported to be broken before macOS 13 - see http://crbug.com/40234870.
// Also, the `SCContentFilter` fields `contentRect` and `pointPixelScale` were
// introduced in macOS 14.
API_AVAILABLE(macos(14.0))
@interface SckHelper : NSObject <SCStreamDelegate, SCStreamOutput>

- (instancetype)initWithCapturer:(ScreenCapturerSckImpl *)capturer;

- (void)onShareableContentCreated:(SCShareableContent *)content generation:(uint64_t)generation
                       revision:(uint64_t)revision;

// Called just before the capturer is destroyed. This avoids a dangling pointer,
// and prevents any new calls into a deleted capturer. If any method-call on the
// capturer is currently running on a different thread, this blocks until it
// completes.
- (void)releaseCapturer;
- (void)privacyCoversChanged:(NSNotification*)notification;
- (void)configurationFinished:(SCStream*)stream generation:(uint64_t)generation
                     revision:(uint64_t)revision complete:(BOOL)complete
                configuration:(SCStreamConfiguration*)configuration error:(NSError*)error;
@end

class API_AVAILABLE(macos(14.0)) ScreenCapturerSckImpl : public ScreenCapturer {
 public:
  explicit ScreenCapturerSckImpl();

  ScreenCapturerSckImpl(const ScreenCapturerSckImpl &) = delete;
  ScreenCapturerSckImpl &operator=(const ScreenCapturerSckImpl &) = delete;
  ~ScreenCapturerSckImpl();

 public:
  int Init(const int fps, cb_desktop_data cb) override;
  void SetPrivacyController(PrivacyController* privacy) override { privacy_ = privacy; }

  int Start(bool show_cursor) override;

  int SwitchTo(int monitor_index) override;

  int Destroy() override;

  int Stop() override;

  int Pause(int monitor_index) override { return 0; }

  int Resume(int monitor_index) override { return 0; }

  std::vector<DisplayInfo> GetDisplayInfoList() override {
    std::lock_guard<std::mutex> lock(lock_);
    return display_info_list_;
  }
  int ResetToInitialMonitor() override;

 private:
  std::vector<DisplayInfo> display_info_list_;
  std::map<int, CGDirectDisplayID> display_id_map_;
  int fps_ = 60;
  bool show_cursor_ = false;
  bool capture_requested_ = false;
  bool invalid_stream_id_logged_ = false;
  bool native_output_logged_ = false;
  PrivacyController* privacy_ = nullptr;
  uint64_t configuration_generation_ = 0;
  bool configuration_in_flight_ = false;
  bool reconfigure_pending_ = false;
  SCStreamConfiguration* __strong applied_configuration_ = nil;

 public:
  // Called by SckHelper when shareable content is returned by ScreenCaptureKit. `content` will be
  // nil if an error occurred. May run on an arbitrary thread.
  void OnShareableContentCreated(SCShareableContent *content, uint64_t generation,
                                uint64_t revision);
  void ConfigurationFinished(SCStream* source, uint64_t generation, uint64_t revision,
                             bool complete, SCStreamConfiguration* configuration,
                             NSError* error);
  void RetryPrivacyFilter();
  void PrivacyCoversChanged() { StartOrReconfigureCapturer(); }
  void CaptureStopped(SCStream* source, NSError* error) {
    {
      std::lock_guard lock(lock_);
      if (!capture_requested_ || source != stream_) return;
    }
    LOG_ERROR("ScreenCaptureKit stream stopped: {}", NSErrorToString(error));
    if (privacy_) privacy_->Fail("ScreenCaptureKit stream stopped");
  }
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
  // Callback for returning captured frames to the caller. ScreenCaptureKit may
  // invoke it on its sample-handler queue.
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
  int current_monitor_index_ = 0;
  std::string current_stream_id_;
};

static std::string NSStringToUtf8(NSString *value) {
  if (!value || value.length == 0) return "";
  const char *utf8 = value.UTF8String;
  return utf8 ? utf8 : "";
}

static std::string GetAppKitDisplayName(CGDirectDisplayID display_id) {
  if (@available(macOS 10.15, *)) {
    @autoreleasepool {
      for (NSScreen *screen in NSScreen.screens) {
        NSNumber *screen_number = screen.deviceDescription[@"NSScreenNumber"];
        if (screen_number && screen_number.unsignedIntValue == display_id) {
          return NSStringToUtf8(screen.localizedName);
        }
      }
    }
  }
  return "";
}

std::string GetDisplayName(CGDirectDisplayID display_id) {
  // IODisplayConnect is no longer exposed for some external displays on
  // modern Apple Silicon Macs. NSScreen is the system-owned source for the
  // user-visible name and maps directly to CGDirectDisplayID.
  std::string appkit_name = GetAppKitDisplayName(display_id);
  if (!appkit_name.empty()) return appkit_name;

  // Keep the IOKit path for older macOS versions and display drivers.
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
  native_output_logged_ = false;

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

  for (SCDisplay *display in content.displays) {
    CGDirectDisplayID display_id = display.displayID;
    CGRect bounds = CGDisplayBounds(display_id);
    bool is_primary = CGDisplayIsMain(display_id);

    std::string name = GetDisplayName(display_id);
    if (name.empty()) {
      name = MakeDisplayStreamId(display_info_list_.size());
    }

    LOG_INFO("macOS display discovered: index={}, display_id={}, name='{}'",
             display_info_list_.size(), display_id, name);

    DisplayInfo info((void *)(uintptr_t)display_id, name, is_primary,
                     static_cast<int>(bounds.origin.x), static_cast<int>(bounds.origin.y),
                     static_cast<int>(bounds.origin.x + bounds.size.width),
                     static_cast<int>(bounds.origin.y + bounds.size.height));

    display_info_list_.push_back(info);
    display_id_map_[display_info_list_.size() - 1] = display_id;
  }

  initial_monitor_index_ = 0;
  current_monitor_index_ = initial_monitor_index_;
  current_display_ = display_id_map_[current_monitor_index_];
  current_stream_id_ = MakeDisplayStreamId(current_monitor_index_);
  return 0;
}

int ScreenCapturerSckImpl::Start(bool show_cursor) {
  if (permanent_error_) {
    LOG_ERROR("Cannot start capturer: permanent error occurred");
    return -1;
  }

  {
    std::lock_guard<std::mutex> lock(lock_);
    if (display_info_list_.empty()) {
      LOG_ERROR("Cannot start capturer: display info not initialized");
      return -1;
    }
    show_cursor_ = show_cursor;
    capture_requested_ = true;
    invalid_stream_id_logged_ = false;
  }
  if (privacy_) privacy_->CaptureChanged(true);
  StartOrReconfigureCapturer();
  return 0;
}

int ScreenCapturerSckImpl::SwitchTo(int monitor_index) {
  bool should_reconfigure = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto display_it = display_id_map_.find(monitor_index);
    if (display_it == display_id_map_.end() || monitor_index < 0 ||
        monitor_index >= static_cast<int>(display_info_list_.size())) {
      LOG_WARN("SwitchTo skipped, invalid monitor_index={}, displays={}",
               monitor_index, display_id_map_.size());
      return -1;
    }
    current_monitor_index_ = monitor_index;
    current_display_ = display_it->second;
    current_stream_id_ = MakeDisplayStreamId(monitor_index);
    should_reconfigure = capture_requested_;
  }
  if (should_reconfigure) {
    StartOrReconfigureCapturer();
  }
  return 0;
}

int ScreenCapturerSckImpl::ResetToInitialMonitor() {
  const int target = initial_monitor_index_;
  bool should_reconfigure = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (display_info_list_.empty()) return -1;
    auto display_it = display_id_map_.find(target);
    if (display_it == display_id_map_.end()) {
      LOG_WARN("ResetToInitialMonitor skipped, invalid monitor_index={}", target);
      return -1;
    }
    const CGDirectDisplayID target_display = display_it->second;
    if (current_display_ == target_display &&
        current_monitor_index_ == target) {
      return 0;
    }
    current_monitor_index_ = target;
    current_display_ = target_display;
    current_stream_id_ = MakeDisplayStreamId(target);
    should_reconfigure = capture_requested_ && stream_ != nil;
  }

  // Resetting session state must not create a capture stream. Preserve the
  // selected monitor for the next Start(), and only reconfigure an active one.
  if (should_reconfigure) {
    StartOrReconfigureCapturer();
  }
  return 0;
}

int ScreenCapturerSckImpl::Destroy() {
  if (privacy_) privacy_->CaptureChanged(false);
  SckHelper *helper_to_release = nil;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (stream_) {
      LOG_INFO("Destroying stream");
      [stream_ stopCaptureWithCompletionHandler:nil];
      stream_ = nil;
    }
    capture_requested_ = false;
    configuration_in_flight_ = reconfigure_pending_ = false;
    applied_configuration_ = nil;
    ++configuration_generation_;
    current_display_ = 0;
    current_stream_id_.clear();
    permanent_error_ = false;
    _on_data = nullptr;
    helper_to_release = helper_;
    helper_ = nil;
  }

  [helper_to_release releaseCapturer];

  return 0;
}

int ScreenCapturerSckImpl::Stop() {
  if (privacy_) privacy_->CaptureChanged(false);
  std::lock_guard<std::mutex> lock(lock_);
  capture_requested_ = false;
  configuration_in_flight_ = reconfigure_pending_ = false;
  applied_configuration_ = nil;
  ++configuration_generation_;
  if (stream_) {
    LOG_INFO("Stopping stream");
    [stream_ stopCaptureWithCompletionHandler:nil];
    stream_ = nil;
  }
  current_display_ = 0;

  return 0;
}

void ScreenCapturerSckImpl::OnShareableContentCreated(SCShareableContent *content,
                                                     uint64_t generation, uint64_t revision) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!capture_requested_ || generation != configuration_generation_) {
      LOG_INFO("Ignoring stale ScreenCaptureKit display refresh after stop");
      return;
    }
  }

  // The window list belongs to the privacy revision present when the request
  // was issued. A newer cover must be enumerated by a new request.
  if (GetMacPrivacyCaptureState().revision != revision) {
    StartOrReconfigureCapturer();
    return;
  }

  if (!content) {
    LOG_ERROR("getShareableContent failed");
    if (privacy_) privacy_->Fail("Could not refresh ScreenCaptureKit privacy content");
    std::lock_guard lock(lock_);
    if (!stream_) permanent_error_ = true;
    return;
  }

  if (!content.displays || content.displays.count == 0) {
    LOG_ERROR("getShareableContent returned no displays");
    if (privacy_) privacy_->Fail("No shareable displays available for privacy");
    std::lock_guard lock(lock_);
    if (!stream_) permanent_error_ = true;
    return;
  }

  SCDisplay *captured_display = nil;
  bool show_cursor = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!capture_requested_ || generation != configuration_generation_) return;

    int logical_index = current_monitor_index_;
    if (logical_index < 0 ||
        logical_index >= static_cast<int>(display_info_list_.size())) {
      logical_index = 0;
    }

    SCDisplay *logical_fallback = nil;
    int display_index = 0;
    for (SCDisplay *display in content.displays) {
      if (display_index == logical_index) logical_fallback = display;
      if (current_display_ != 0 && current_display_ == display.displayID) {
        captured_display = display;
        break;
      }
      ++display_index;
    }
    if (!captured_display) {
      captured_display = logical_fallback ? logical_fallback
                                          : content.displays.firstObject;
      if (!logical_fallback) logical_index = 0;
    }

    if (captured_display) {
      const CGDirectDisplayID old_display = current_display_;
      const CGDirectDisplayID new_display = captured_display.displayID;
      const std::string stable_stream_id = ResolveDisplayStreamId(
          current_stream_id_.c_str(), display_info_list_.size(), logical_index);
      if (stable_stream_id.empty()) {
        LOG_ERROR("Cannot map macOS display {} to a registered video stream",
                  new_display);
        return;
      }

      current_monitor_index_ = logical_index;
      current_display_ = new_display;
      current_stream_id_ = stable_stream_id;
      display_id_map_[logical_index] = new_display;

      CGRect bounds = CGDisplayBounds(new_display);
      auto& info = display_info_list_[logical_index];
      info.handle = (void *)(uintptr_t)new_display;
      info.is_primary = CGDisplayIsMain(new_display);
      info.left = static_cast<int>(bounds.origin.x);
      info.top = static_cast<int>(bounds.origin.y);
      info.right = static_cast<int>(bounds.origin.x + bounds.size.width);
      info.bottom = static_cast<int>(bounds.origin.y + bounds.size.height);
      info.width = info.right - info.left;
      info.height = info.bottom - info.top;
      const std::string refreshed_name = GetDisplayName(new_display);
      if (!refreshed_name.empty()) {
        info.name = refreshed_name;
      }

      if (old_display != new_display) {
        LOG_INFO("macOS display mapping refreshed: slot={}, old_id={}, "
                 "new_id={}, stream='{}'",
                 logical_index, old_display, new_display, stable_stream_id);
      }
    }
    show_cursor = show_cursor_;
  }

  if (!captured_display) {
    LOG_ERROR("Failed to find valid display");
    permanent_error_ = true;
    return;
  }

  const auto privacy_capture = GetMacPrivacyCaptureState();
  // The cursor sprite is included only when this stream embeds the cursor.
  // Other controllers receive the unchanged cursor metadata separately.
  auto excluded_ids = privacy_capture.excluded_windows;
  if (!show_cursor && privacy_capture.cursor_window)
    excluded_ids.push_back(privacy_capture.cursor_window);
  NSMutableArray<SCWindow*>* excluded = [NSMutableArray array];
  for (SCWindow* window in content.windows) {
    if (std::find(excluded_ids.begin(), excluded_ids.end(), window.windowID) != excluded_ids.end())
      [excluded addObject:window];
  }
  // Newly registered transparent covers may need another enumeration. Never
  // acknowledge a partial exclusion or remove an active cover's filter.
  const bool privacy_filter_complete = excluded.count == excluded_ids.size();
  LOG_INFO("macOS privacy filter: generation={}, revision={}, requested={}, excluded={}",
           generation, privacy_capture.revision, excluded_ids.size(),
           excluded.count);
  if (!privacy_filter_complete) {
    bool have_stream;
    { std::lock_guard lock(lock_); have_stream = stream_ != nil; }
    if (privacy_capture.applied_revision == privacy_capture.revision) {
      if (privacy_) privacy_->Fail("ScreenCaptureKit could not enumerate every privacy cover");
      return; // Preserve the existing exclusion until recovery removes covers.
    }
    if (have_stream) {
      RetryPrivacyFilter();
      return; // Transparent covers are still being registered by WindowServer.
    }
    // Start ordinary capture even if a newly created, still-transparent cover
    // is not enumerable yet. Retry exclusion after start; never acknowledge it.
    [excluded removeAllObjects];
  }
  SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:captured_display
                                                    excludingWindows:excluded];
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
  config.showsCursor = show_cursor;
  config.width = filter.contentRect.size.width * filter.pointPixelScale;
  config.height = filter.contentRect.size.height * filter.pointPixelScale;
  config.captureResolution = SCCaptureResolutionAutomatic;
  config.minimumFrameInterval = CMTimeMake(1, fps_);

  std::unique_lock<std::mutex> lock(lock_);
  if (!capture_requested_ || generation != configuration_generation_) return;
  if (configuration_in_flight_) { reconfigure_pending_ = true; return; }
  if (GetMacPrivacyCaptureState().revision != revision) {
    lock.unlock();
    RetryPrivacyFilter();
    return;
  }
  configuration_in_flight_ = true;

  if (stream_) {
    SckHelper* helper = helper_;
    SCStream* source = stream_;
    auto finished = ^(NSError* error) {
      [helper configurationFinished:source generation:generation revision:revision
                            complete:privacy_filter_complete configuration:config error:error];
    };
    // Apply configuration before its filter and acknowledge the final update.
    // Privacy-only toggles do not need a configuration change.
    const bool same_config = applied_configuration_ &&
        applied_configuration_.width == config.width &&
        applied_configuration_.height == config.height &&
        applied_configuration_.showsCursor == config.showsCursor &&
        applied_configuration_.pixelFormat == config.pixelFormat &&
        CMTimeCompare(applied_configuration_.minimumFrameInterval, config.minimumFrameInterval) == 0;
    if (same_config) {
      [source updateContentFilter:filter completionHandler:finished];
    } else {
      [source updateConfiguration:config completionHandler:^(NSError* error) {
        if (error) finished(error);
        else [source updateContentFilter:filter completionHandler:finished];
      }];
    }
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
      configuration_in_flight_ = false;
      stream_ = nil;
      LOG_ERROR("addStreamOutput failed: {}", NSErrorToString(add_stream_output_error));
      permanent_error_ = true;
      return;
    }

    SckHelper* helper = helper_;
    SCStream* source = stream_;
    auto handler = ^(NSError *error) {
      [helper configurationFinished:source generation:generation revision:revision
                            complete:privacy_filter_complete configuration:config error:error];
    };

    [stream_ startCaptureWithCompletionHandler:handler];
  }
}

void ScreenCapturerSckImpl::RetryPrivacyFilter() {
  SckHelper* helper;
  {
    std::lock_guard lock(lock_);
    if (!capture_requested_) return;
    helper = helper_;
  }
  // The retained helper safely ignores this callback after Destroy().
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{ [helper privacyCoversChanged:nil]; });
}

void ScreenCapturerSckImpl::ConfigurationFinished(SCStream* source, uint64_t generation,
                                                  uint64_t revision, bool complete,
                                                  SCStreamConfiguration* configuration,
                                                  NSError* error) {
  bool refresh = false;
  {
    std::lock_guard lock(lock_);
    if (!capture_requested_ || source != stream_) return;
    configuration_in_flight_ = false;
    const auto current = GetMacPrivacyCaptureState();
    refresh = reconfigure_pending_ || current.revision != revision;
    reconfigure_pending_ = false;
    if (!error) {
      applied_configuration_ = configuration;
      if (complete && !refresh && generation == configuration_generation_)
        AcknowledgeMacPrivacyCapture(revision);
      refresh = refresh || !complete;
    }
    LOG_INFO("macOS privacy filter completed: generation={}, revision={}, complete={}, refresh={}, error={}",
             generation, revision, complete, refresh, NSErrorToString(error));
  }
  if (error && privacy_)
    privacy_->Fail("ScreenCaptureKit privacy filter: " + NSErrorToString(error));
  if (refresh) RetryPrivacyFilter();
}

void ScreenCapturerSckImpl::OnNewCVPixelBuffer(CVPixelBufferRef pixelBuffer,
                                               CFDictionaryRef attachment) {
  (void)attachment;
  if (!pixelBuffer) {
    return;
  }

  const size_t width = CVPixelBufferGetWidth(pixelBuffer);
  const size_t height = CVPixelBufferGetHeight(pixelBuffer);
  const OSType pixel_format = CVPixelBufferGetPixelFormatType(pixelBuffer);
  if (width == 0 || height == 0 || (width & 1U) != 0 ||
      (height & 1U) != 0 || !CVPixelBufferIsPlanar(pixelBuffer) ||
      CVPixelBufferGetPlaneCount(pixelBuffer) < 2 ||
      (pixel_format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange &&
       pixel_format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)) {
    LOG_ERROR("Invalid CVPixelBuffer: width={}, height={}, format={}, planes={}",
              width, height, pixel_format,
              CVPixelBufferGetPlaneCount(pixelBuffer));
    return;
  }

  const size_t required_size = width * height * 3U / 2U;
  if (required_size > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    LOG_ERROR("Captured frame is too large: {} bytes", required_size);
    return;
  }

  cb_desktop_data callback;
  std::string stream_id;
  bool log_native_output = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!_on_data || !capture_requested_) {
      return;
    }
    stream_id = ResolveDisplayStreamId(current_stream_id_.c_str(),
                                       display_info_list_.size(),
                                       current_monitor_index_);
    if (stream_id.empty()) {
      if (!invalid_stream_id_logged_) {
        LOG_ERROR("Dropping macOS frames without a registered stream id, "
                  "display_id={}",
                  current_display_);
        invalid_stream_id_logged_ = true;
      }
      return;
    }
    invalid_stream_id_logged_ = false;
    callback = _on_data;
    if (!native_output_logged_) {
      native_output_logged_ = true;
      log_native_output = true;
    }
  }

  MiniRtcNativeVideoFrame native_frame{};
  native_frame.struct_size = sizeof(native_frame);
  native_frame.type = MiniRtcNativeVideoFrameCVPixelBuffer;
  native_frame.width = static_cast<uint32_t>(width);
  native_frame.height = static_cast<uint32_t>(height);
  native_frame.payload.cv_pixel_buffer = pixelBuffer;
  native_frame.owner = pixelBuffer;
  native_frame.retain = &RetainCapturedPixelBuffer;
  native_frame.release = &ReleaseCapturedPixelBuffer;
  native_frame.copy_to_nv12 = &CopyCapturedPixelBufferToNv12;
  if (log_native_output) {
    LOG_INFO("macOS ScreenCaptureKit native CVPixelBuffer output enabled");
  }
  callback(nullptr, static_cast<int>(required_size), static_cast<int>(width),
           static_cast<int>(height), stream_id.c_str(), &native_frame);
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

  SckHelper *local_helper;
  uint64_t generation;
  uint64_t revision;
  {
    std::lock_guard lock(lock_);
    if (!capture_requested_) return;
    if (configuration_in_flight_) { reconfigure_pending_ = true; return; }
    reconfigure_pending_ = false;
    local_helper = helper_;
    generation = ++configuration_generation_;
    revision = GetMacPrivacyCaptureState().revision;
  }
  if (!local_helper) {
    LOG_ERROR("Cannot reconfigure capturer: helper is null");
    return;
  }

  auto handler = ^(SCShareableContent *content, NSError *error) {
    if (error) {
      LOG_ERROR("getShareableContent failed: {}", NSErrorToString(error));
      [local_helper onShareableContentCreated:nil generation:generation revision:revision];
      return;
    }
    [local_helper onShareableContentCreated:content generation:generation revision:revision];
  };
  [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                           onScreenWindowsOnly:NO
                                             completionHandler:handler];
}

@implementation SckHelper {
  // This lock is to prevent the capturer being destroyed while an instance
  // method is still running on another thread.
  std::mutex _capturer_lock;
  ScreenCapturerSckImpl *_capturer;
  dispatch_queue_t _completion_queue;
}

- (instancetype)initWithCapturer:(ScreenCapturerSckImpl *)capturer {
  self = [super init];
  if (self) {
    _capturer = capturer;
    _completion_queue = dispatch_queue_create("CrossDesk.ScreenCaptureKit.Completions",
                                              DISPATCH_QUEUE_SERIAL);
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(privacyCoversChanged:)
        name:@"CrossDeskPrivacyCoversChanged" object:nil];
  }
  return self;
}

- (void)privacyCoversChanged:(NSNotification*)notification {
  std::lock_guard<std::mutex> lock(_capturer_lock);
  if (_capturer) _capturer->PrivacyCoversChanged();
}
- (void)configurationFinished:(SCStream*)stream generation:(uint64_t)generation
                     revision:(uint64_t)revision complete:(BOOL)complete
                configuration:(SCStreamConfiguration*)configuration error:(NSError*)error {
  // SCK may call a completion inline (notably updateContentFilter when nothing
  // changed). The caller can still hold both _capturer_lock and the capturer's
  // state lock. Always defer before acquiring either lock, including errors.
  // The block retains this helper; releaseCapturer revokes queued callbacks
  // before the C++ capturer is destroyed.
  dispatch_async(_completion_queue, ^{
    std::lock_guard<std::mutex> lock(self->_capturer_lock);
    if (self->_capturer)
      self->_capturer->ConfigurationFinished(stream, generation, revision, complete,
                                             configuration, error);
  });
}
- (void)onShareableContentCreated:(SCShareableContent *)content generation:(uint64_t)generation
                       revision:(uint64_t)revision {
  dispatch_async(_completion_queue, ^{
    std::lock_guard<std::mutex> lock(self->_capturer_lock);
    if (self->_capturer)
      self->_capturer->OnShareableContentCreated(content, generation, revision);
  });
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
  [NSNotificationCenter.defaultCenter removeObserver:self];
  std::lock_guard<std::mutex> lock(_capturer_lock);
  _capturer = nullptr;
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
  dispatch_async(_completion_queue, ^{
    std::lock_guard<std::mutex> lock(self->_capturer_lock);
    if (self->_capturer) self->_capturer->CaptureStopped(stream, error);
  });
}

@end

std::unique_ptr<ScreenCapturer> ScreenCapturerSck::CreateScreenCapturerSck() {
  return std::make_unique<ScreenCapturerSckImpl>();
}
