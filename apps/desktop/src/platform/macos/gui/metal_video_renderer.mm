#include "platform/macos/gui/metal_video_renderer.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

#include "platform/window_drag.h"
#include "rd_log.h"

@interface CrossDeskMetalVideoView : NSView
@end

@interface CrossDeskStreamTitleView : NSView
@property(nonatomic, copy) BOOL (^windowLayoutHandler)(NSWindow* window);
@property(nonatomic, copy) void (^surfaceInvalidationHandler)(void);
@property(nonatomic) BOOL windowLayoutPending;
- (instancetype)initWithTitle:(NSString*)title;
@end

@implementation CrossDeskMetalVideoView

- (CALayer*)makeBackingLayer {
  return [CAMetalLayer layer];
}

- (BOOL)isOpaque {
  return YES;
}

- (NSView*)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}

@end

@implementation CrossDeskStreamTitleView {
  NSTextField* _titleLabel;
}

- (instancetype)initWithTitle:(NSString*)title {
  NSFont* font = [NSFont boldSystemFontOfSize:13.0];
  const CGFloat text_width =
      ceil([title sizeWithAttributes:@{NSFontAttributeName : font}].width);
  self = [super initWithFrame:NSMakeRect(0.0, 0.0, text_width + 10.0, 28.0)];
  if (self != nil) {
    _titleLabel = [NSTextField labelWithString:title];
    _titleLabel.font = font;
    _titleLabel.lineBreakMode = NSLineBreakByClipping;
    _titleLabel.maximumNumberOfLines = 1;
    [self addSubview:_titleLabel];
  }
  return self;
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSWindowDidBecomeKeyNotification
              object:nil];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSWindowDidResignKeyNotification
              object:nil];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSWindowDidResizeNotification
              object:nil];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSWindowDidUpdateNotification
              object:nil];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSWindowDidChangeBackingPropertiesNotification
              object:nil];
  if (self.window != nil) {
    self.windowLayoutPending = YES;
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(windowActivationChanged:)
               name:NSWindowDidBecomeKeyNotification
             object:self.window];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(windowActivationChanged:)
               name:NSWindowDidResignKeyNotification
             object:self.window];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(windowDidResize:)
               name:NSWindowDidResizeNotification
             object:self.window];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(windowDidUpdate:)
               name:NSWindowDidUpdateNotification
             object:self.window];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(windowBackingPropertiesChanged:)
               name:NSWindowDidChangeBackingPropertiesNotification
             object:self.window];
  }
  [self invalidateVideoSurface];
  [self updateTitleColor];
}

- (void)viewDidChangeEffectiveAppearance {
  [super viewDidChangeEffectiveAppearance];
  [self updateTitleColor];
  [self invalidateVideoSurface];
  self.windowLayoutPending = YES;
  if (self.windowLayoutHandler != nil && self.window != nil) {
    self.windowLayoutPending = !self.windowLayoutHandler(self.window);
  }
}

- (void)windowActivationChanged:(NSNotification*)notification {
  (void)notification;
  [self updateTitleColor];
}

- (void)windowDidResize:(NSNotification*)notification {
  [self invalidateVideoSurface];
  self.windowLayoutPending = YES;
  if (self.windowLayoutHandler != nil &&
      [notification.object isKindOfClass:NSWindow.class]) {
    self.windowLayoutHandler(static_cast<NSWindow*>(notification.object));
  }
}

- (void)windowBackingPropertiesChanged:(NSNotification*)notification {
  (void)notification;
  [self invalidateVideoSurface];
}

- (void)windowDidUpdate:(NSNotification*)notification {
  if (!self.windowLayoutPending || self.windowLayoutHandler == nil ||
      ![notification.object isKindOfClass:NSWindow.class]) {
    return;
  }
  self.windowLayoutPending =
      !self.windowLayoutHandler(static_cast<NSWindow*>(notification.object));
  [self invalidateVideoSurface];
}

- (void)invalidateVideoSurface {
  if (self.surfaceInvalidationHandler != nil) {
    self.surfaceInvalidationHandler();
  }
}

- (void)updateTitleColor {
  _titleLabel.textColor = self.window.isKeyWindow
                              ? NSColor.blackColor
                              : [NSColor colorWithWhite:0.45 alpha:1.0];
}

- (void)layout {
  [super layout];
  const NSSize title_size = _titleLabel.fittingSize;
  _titleLabel.frame = NSMakeRect(
      8.0, floor((self.bounds.size.height - title_size.height) * 0.5),
      title_size.width, title_size.height);
}

- (NSView*)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}

- (BOOL)mouseDownCanMoveWindow {
  return YES;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end

namespace crossdesk {
namespace {

constexpr size_t kFrameSlotCount = 3;

// Slint's winit backend creates AppKit windows with transparent backing so
// transparent Slint items can be composited. Keep the content out of the
// titlebar and choose whether AppKit's material or the semantic window color
// is visible behind the native titlebar controls.
bool ConfigureNativeWindowChrome(NSWindow* window,
                                 BOOL titlebar_transparent) {
  if (window == nil ||
      (window.styleMask & NSWindowStyleMaskTitled) == 0) {
    return false;
  }

  bool layout_changed = false;
  if ((window.styleMask & NSWindowStyleMaskFullSizeContentView) != 0) {
    window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
    layout_changed = true;
  }
  if (window.titlebarAppearsTransparent != titlebar_transparent) {
    window.titlebarAppearsTransparent = titlebar_transparent;
    layout_changed = true;
  }
  window.opaque = YES;
  window.backgroundColor = NSColor.windowBackgroundColor;

  // Removing FullSizeContentView changes the titlebar's layout guide. Commit
  // the public titlebar-accessory layout before the next frame is presented.
  if (layout_changed) {
    NSView* frame_view = window.contentView.superview;
    frame_view.needsLayout = YES;
    [frame_view layoutSubtreeIfNeeded];
  }
  return (window.styleMask & NSWindowStyleMaskFullSizeContentView) == 0 &&
         window.titlebarAppearsTransparent == titlebar_transparent &&
         window.opaque;
}

bool RestoreNativeWindowChrome(NSWindow* window) {
  return ConfigureNativeWindowChrome(window, NO);
}

bool ConfigureStreamWindowChrome(NSWindow* window) {
  // Keep the stream titlebar pure white in every system appearance. Its
  // transparent native titlebar exposes this fixed window backing color while
  // the Slint and Metal content continues to follow the selected theme.
  const bool configured = ConfigureNativeWindowChrome(window, YES);
  if (window != nil) {
    window.backgroundColor = NSColor.whiteColor;
  }
  return configured;
}

size_t AlignUp(size_t value, size_t alignment) {
  if (alignment <= 1) {
    return value;
  }
  return ((value + alignment - 1) / alignment) * alignment;
}

enum class SlotUse {
  available,
  pending,
  in_flight,
};

struct FrameSlot {
  id<MTLBuffer> y_buffer = nil;
  id<MTLBuffer> uv_buffer = nil;
  id<MTLTexture> y_texture = nil;
  id<MTLTexture> uv_texture = nil;
  size_t y_stride = 0;
  size_t uv_stride = 0;
  int width = 0;
  int height = 0;
  std::string remote_id;
  uint64_t sequence = 0;
  SlotUse use = SlotUse::available;
  bool valid = false;

  void ReleaseResources() {
    y_texture = nil;
    uv_texture = nil;
    y_buffer = nil;
    uv_buffer = nil;
    y_stride = 0;
    uv_stride = 0;
    width = 0;
    height = 0;
    valid = false;
  }

  ~FrameSlot() { ReleaseResources(); }
};

struct SharedFrameState {
  mutable std::mutex mutex;
  std::array<FrameSlot, kFrameSlotCount> slots;
  std::string selected_stream;
  uint64_t next_sequence = 1;
  uint64_t content_generation = 1;
};

bool AllocateSlot(id<MTLDevice> device, FrameSlot& slot, int width,
                  int height) {
  if (slot.y_buffer != nil && slot.uv_buffer != nil &&
      slot.y_texture != nil && slot.uv_texture != nil &&
      slot.width == width && slot.height == height) {
    return true;
  }

  slot.ReleaseResources();

  const size_t y_alignment = std::max<size_t>(
      1, [device minimumLinearTextureAlignmentForPixelFormat:MTLPixelFormatR8Unorm]);
  const size_t uv_alignment = std::max<size_t>(
      1, [device minimumLinearTextureAlignmentForPixelFormat:MTLPixelFormatRG8Unorm]);
  slot.y_stride = AlignUp(static_cast<size_t>(width), y_alignment);
  slot.uv_stride = AlignUp(static_cast<size_t>(width), uv_alignment);

  const size_t y_length = slot.y_stride * static_cast<size_t>(height);
  const size_t uv_length =
      slot.uv_stride * static_cast<size_t>(height / 2);
  const MTLResourceOptions options =
      MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined;
  slot.y_buffer = [device newBufferWithLength:y_length options:options];
  slot.uv_buffer = [device newBufferWithLength:uv_length options:options];
  if (slot.y_buffer == nil || slot.uv_buffer == nil) {
    slot.ReleaseResources();
    return false;
  }

  MTLTextureDescriptor* y_descriptor =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                         width:width
                                                        height:height
                                                     mipmapped:NO];
  y_descriptor.storageMode = MTLStorageModeShared;
  y_descriptor.usage = MTLTextureUsageShaderRead;
  slot.y_texture = [slot.y_buffer newTextureWithDescriptor:y_descriptor
                                                   offset:0
                                              bytesPerRow:slot.y_stride];

  MTLTextureDescriptor* uv_descriptor =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
                                                         width:width / 2
                                                        height:height / 2
                                                     mipmapped:NO];
  uv_descriptor.storageMode = MTLStorageModeShared;
  uv_descriptor.usage = MTLTextureUsageShaderRead;
  slot.uv_texture = [slot.uv_buffer newTextureWithDescriptor:uv_descriptor
                                                     offset:0
                                                bytesPerRow:slot.uv_stride];
  if (slot.y_texture == nil || slot.uv_texture == nil) {
    slot.ReleaseResources();
    return false;
  }

  slot.width = width;
  slot.height = height;
  return true;
}

NSString* Nv12ShaderSource() {
  return @R"metal(
#include <metal_stdlib>
using namespace metal;

struct RasterData {
  float4 position [[position]];
  float2 texcoord;
};

vertex RasterData nv12_vertex(uint vertex_id [[vertex_id]]) {
  constexpr float2 positions[] = {
    float2(-1.0, -1.0), float2( 1.0, -1.0),
    float2(-1.0,  1.0), float2( 1.0,  1.0)
  };
  constexpr float2 texcoords[] = {
    float2(0.0, 1.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 0.0)
  };
  RasterData out;
  out.position = float4(positions[vertex_id], 0.0, 1.0);
  out.texcoord = texcoords[vertex_id];
  return out;
}

fragment half4 nv12_fragment(
    RasterData in [[stage_in]],
    texture2d<float, access::sample> y_texture [[texture(0)]],
    texture2d<float, access::sample> uv_texture [[texture(1)]]) {
  constexpr sampler video_sampler(coord::normalized,
                                  address::clamp_to_edge,
                                  filter::linear);
  const float y = 1.16438356 * (y_texture.sample(video_sampler, in.texcoord).r
                                - 16.0 / 255.0);
  const float2 uv = uv_texture.sample(video_sampler, in.texcoord).rg - 0.5;
  // BT.601 limited-range NV12, matching the previous libyuv NV12ToABGR/RAW
  // path. MiniRTC's current XVideoFrame API does not carry color metadata, so
  // retaining the established matrix avoids a platform-only color change.
  const float3 rgb = float3(y + 1.59602678 * uv.y,
                            y - 0.39176229 * uv.x - 0.81296764 * uv.y,
                            y + 2.01723214 * uv.x);
  return half4(half3(saturate(rgb)), 1.0h);
}
)metal";
}

}  // namespace

struct MacMetalVideoRenderer::Impl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> command_queue = nil;
  id<MTLRenderPipelineState> pipeline = nil;
  std::shared_ptr<SharedFrameState> frames =
      std::make_shared<SharedFrameState>();
  NSView* slint_view = nil;
  CrossDeskMetalVideoView* video_view = nil;
  NSTitlebarAccessoryViewController* title_accessory_controller = nil;
  CAMetalLayer* metal_layer = nil;
  std::atomic<bool> attached{false};
  std::string rendered_stream;
  uint64_t rendered_content_generation = 0;
  std::shared_ptr<std::atomic<bool>> native_surface_needs_redraw =
      std::make_shared<std::atomic<bool>>(true);

  Impl() {
    @autoreleasepool {
      device = MTLCreateSystemDefaultDevice();
      if (device == nil) {
        LOG_ERROR("Metal video renderer unavailable: no Metal device");
        return;
      }
      command_queue = [device newCommandQueue];
      if (command_queue == nil) {
        LOG_ERROR("Metal video renderer unavailable: command queue creation failed");
        return;
      }

      NSError* library_error = nil;
      id<MTLLibrary> library =
          [device newLibraryWithSource:Nv12ShaderSource()
                               options:nil
                                 error:&library_error];
      if (library == nil) {
        const char* message = library_error.localizedDescription.UTF8String;
        LOG_ERROR("Metal NV12 shader compilation failed: {}",
                  message ? message : "unknown error");
        return;
      }

      id<MTLFunction> vertex = [library newFunctionWithName:@"nv12_vertex"];
      id<MTLFunction> fragment =
          [library newFunctionWithName:@"nv12_fragment"];
      MTLRenderPipelineDescriptor* descriptor =
          [[MTLRenderPipelineDescriptor alloc] init];
      descriptor.vertexFunction = vertex;
      descriptor.fragmentFunction = fragment;
      descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

      NSError* pipeline_error = nil;
      pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
                                                        error:&pipeline_error];
      if (pipeline == nil) {
        const char* message = pipeline_error.localizedDescription.UTF8String;
        LOG_ERROR("Metal NV12 pipeline creation failed: {}",
                  message ? message : "unknown error");
      }
    }
  }

  ~Impl() {
    DetachViews();
    frames.reset();
  }

  bool Ready() const {
    return device != nil && command_queue != nil && pipeline != nil;
  }

  void DetachViews() {
    @autoreleasepool {
      attached.store(false, std::memory_order_release);
      NSWindow* window = slint_view.window;
      if (video_view != nil) {
        [video_view removeFromSuperview];
      }
      if (title_accessory_controller != nil && window != nil) {
        const NSUInteger index = [window.titlebarAccessoryViewControllers
            indexOfObjectIdenticalTo:title_accessory_controller];
        if (index != NSNotFound) {
          [window removeTitlebarAccessoryViewControllerAtIndex:index];
        }
      }
      title_accessory_controller = nil;
      if (window != nil && !IsStreamWindowFullscreen()) {
        RestoreNativeWindowChrome(window);
        window.titleVisibility = NSWindowTitleVisible;
        NSView* frame_view = window.contentView.superview;
        frame_view.needsLayout = YES;
        [frame_view layoutSubtreeIfNeeded];
      }
      video_view = nil;
      slint_view = nil;
      metal_layer = nil;
      rendered_stream.clear();
      rendered_content_generation = 0;
      native_surface_needs_redraw->store(true, std::memory_order_release);
    }
  }

  MacMetalVideoRenderer::RenderResult MissingDrawableResult() const {
    native_surface_needs_redraw->store(true, std::memory_order_release);
    NSWindow* window = slint_view.window;
    if (window == nil || !window.isVisible || window.isMiniaturized) {
      return MacMetalVideoRenderer::RenderResult::idle;
    }
    return MacMetalVideoRenderer::RenderResult::failed;
  }

  void RestoreVideoViewOrder() {
    if (slint_view == nil || video_view == nil) {
      return;
    }
    NSView* parent = slint_view.superview;
    if (parent == nil || video_view.superview != parent) {
      return;
    }

    NSArray<NSView*>* siblings = parent.subviews;
    const NSUInteger video_index = [siblings indexOfObjectIdenticalTo:video_view];
    const NSUInteger slint_index = [siblings indexOfObjectIdenticalTo:slint_view];
    if (video_index != NSNotFound && slint_index != NSNotFound &&
        video_index + 1 == slint_index) {
      return;
    }

    // AppKit may insert an appearance material view between these siblings
    // while switching themes. Keep the video immediately below Slint so that
    // native window chrome stays below the video instead of covering it.
    [parent addSubview:video_view
             positioned:NSWindowBelow
             relativeTo:slint_view];
    native_surface_needs_redraw->store(true, std::memory_order_release);
  }

  void UpdateVideoGeometry(double top_inset_points,
                           bool native_titlebar_visible) {
    if (slint_view == nil || video_view == nil || metal_layer == nil) {
      return;
    }
    NSView* parent = slint_view.superview;
    if (parent == nil) {
      return;
    }
    RestoreVideoViewOrder();

    NSRect frame = slint_view.frame;
    NSWindow* window = slint_view.window;
    if (title_accessory_controller != nil) {
      title_accessory_controller.view.hidden = !native_titlebar_visible;
    }
    if (native_titlebar_visible && window != nil &&
        (window.styleMask & NSWindowStyleMaskTitled) != 0 &&
        parent.window == window) {
      // Winit may use NSWindowStyleMaskFullSizeContentView, in which case its
      // NSView extends behind the native titlebar. contentLayoutRect is the
      // AppKit-owned, non-obscured content area in window coordinates.
      const NSRect content_layout_in_parent =
          [parent convertRect:window.contentLayoutRect fromView:nil];
      const NSRect visible_content =
          NSIntersectionRect(frame, content_layout_in_parent);
      if (!NSIsEmptyRect(visible_content)) {
        frame = visible_content;
      }
    }
    const CGFloat inset = std::clamp<CGFloat>(
        static_cast<CGFloat>(top_inset_points), 0.0, frame.size.height);
    if (parent.isFlipped) {
      frame.origin.y += inset;
    }
    frame.size.height = std::max<CGFloat>(0.0, frame.size.height - inset);
    if (!NSEqualRects(video_view.frame, frame)) {
      video_view.frame = frame;
      native_surface_needs_redraw->store(true, std::memory_order_release);
    }

    const CGFloat scale = std::max<CGFloat>(
        1.0, slint_view.window ? slint_view.window.backingScaleFactor : 1.0);
    metal_layer.contentsScale = scale;
    const CGSize drawable_size =
        CGSizeMake(std::max<CGFloat>(1.0, video_view.bounds.size.width * scale),
                   std::max<CGFloat>(1.0,
                                     video_view.bounds.size.height * scale));
    if (!CGSizeEqualToSize(metal_layer.drawableSize, drawable_size)) {
      metal_layer.drawableSize = drawable_size;
      native_surface_needs_redraw->store(true, std::memory_order_release);
    }
  }
};

MacMetalVideoRenderer::MacMetalVideoRenderer()
    : impl_(std::make_unique<Impl>()) {}

MacMetalVideoRenderer::~MacMetalVideoRenderer() = default;

bool MacMetalVideoRenderer::IsReady() const {
  return impl_ && impl_->Ready();
}

bool MacMetalVideoRenderer::IsActive() const {
  return IsReady() && IsAttached();
}

bool MacMetalVideoRenderer::SetSelectedStream(std::string remote_id) {
  if (!impl_) {
    return false;
  }
  std::lock_guard lock(impl_->frames->mutex);
  if (impl_->frames->selected_stream == remote_id) {
    return false;
  }
  impl_->frames->selected_stream = std::move(remote_id);
  ++impl_->frames->content_generation;
  for (auto& slot : impl_->frames->slots) {
    if (slot.use == SlotUse::pending) {
      slot.use = SlotUse::available;
    }
  }
  impl_->native_surface_needs_redraw->store(true,
                                            std::memory_order_release);
  return true;
}

void MacMetalVideoRenderer::DiscardStream(std::string_view remote_id) {
  if (!impl_ || remote_id.empty()) {
    return;
  }
  std::lock_guard lock(impl_->frames->mutex);
  bool discarded = false;
  for (auto& slot : impl_->frames->slots) {
    if (!slot.valid || slot.remote_id != remote_id) {
      continue;
    }
    slot.valid = false;
    if (slot.use == SlotUse::pending) {
      slot.use = SlotUse::available;
    }
    discarded = true;
  }
  if (discarded || impl_->frames->selected_stream == remote_id) {
    ++impl_->frames->content_generation;
    impl_->native_surface_needs_redraw->store(true,
                                              std::memory_order_release);
  }
}

MacMetalVideoRenderer::SubmitResult MacMetalVideoRenderer::SubmitNv12(
    std::string_view remote_id, const uint8_t* data, size_t size, int width,
    int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, true);
}

MacMetalVideoRenderer::SubmitResult MacMetalVideoRenderer::SubmitCachedNv12(
    std::string_view remote_id, const uint8_t* data, size_t size, int width,
    int height) {
  return SubmitNv12Internal(remote_id, data, size, width, height, false);
}

MacMetalVideoRenderer::SubmitResult MacMetalVideoRenderer::SubmitNv12Internal(
    std::string_view remote_id, const uint8_t* data, size_t size, int width,
    int height, bool replace_pending) {
  if (!IsReady()) {
    return SubmitResult::failed;
  }

  auto& frames = *impl_->frames;
  std::lock_guard lock(frames.mutex);
  if (frames.selected_stream != remote_id) {
    return SubmitResult::not_selected;
  }
  if (data == nullptr || width <= 0 || height <= 0 || (width & 1) != 0 ||
      (height & 1) != 0) {
    return SubmitResult::failed;
  }
  const size_t y_size = static_cast<size_t>(width) * height;
  const size_t required_size = y_size + y_size / 2;
  if (size < required_size) {
    return SubmitResult::failed;
  }

  FrameSlot* target = nullptr;
  if (replace_pending) {
    for (auto& slot : frames.slots) {
      if (slot.use == SlotUse::pending) {
        target = &slot;
        break;
      }
    }
  } else {
    for (const auto& slot : frames.slots) {
      if (slot.valid && slot.remote_id == remote_id &&
          (slot.use == SlotUse::pending ||
           slot.use == SlotUse::in_flight)) {
        return SubmitResult::dropped;
      }
    }
  }
  if (target == nullptr) {
    uint64_t oldest_sequence = std::numeric_limits<uint64_t>::max();
    for (auto& slot : frames.slots) {
      if (slot.use != SlotUse::available) {
        continue;
      }
      if (!slot.valid) {
        target = &slot;
        break;
      }
      if (slot.sequence < oldest_sequence) {
        oldest_sequence = slot.sequence;
        target = &slot;
      }
    }
  }
  if (target == nullptr) {
    return SubmitResult::dropped;
  }
  if (!AllocateSlot(impl_->device, *target, width, height)) {
    return SubmitResult::failed;
  }

  auto* y_destination = static_cast<uint8_t*>(target->y_buffer.contents);
  auto* uv_destination = static_cast<uint8_t*>(target->uv_buffer.contents);
  const uint8_t* uv_source = data + y_size;
  for (int row = 0; row < height; ++row) {
    std::memcpy(y_destination + static_cast<size_t>(row) * target->y_stride,
                data + static_cast<size_t>(row) * width, width);
  }
  for (int row = 0; row < height / 2; ++row) {
    std::memcpy(uv_destination + static_cast<size_t>(row) * target->uv_stride,
                uv_source + static_cast<size_t>(row) * width, width);
  }

  target->remote_id.assign(remote_id);
  target->sequence = frames.next_sequence++;
  target->use = SlotUse::pending;
  target->valid = true;
  return SubmitResult::submitted;
}

bool MacMetalVideoRenderer::Attach(void* opaque_slint_view) {
  if (!IsReady() || opaque_slint_view == nullptr) {
    return false;
  }
  @autoreleasepool {
    NSView* requested_view = (__bridge NSView*)opaque_slint_view;
    if (impl_->slint_view == requested_view && impl_->video_view != nil &&
        impl_->video_view.superview != nil) {
      if (!IsStreamWindowFullscreen()) {
        ConfigureStreamWindowChrome(requested_view.window);
        requested_view.window.titleVisibility = NSWindowTitleHidden;
        impl_->title_accessory_controller.view.hidden = NO;
      }
      return true;
    }
    NSWindow* window = requested_view.window;
    if (requested_view.superview == nil || window == nil) {
      return false;
    }

    impl_->DetachViews();
    ConfigureStreamWindowChrome(window);
    impl_->slint_view = requested_view;
    impl_->slint_view.wantsLayer = YES;
    impl_->slint_view.layer.opaque = NO;
    impl_->slint_view.layer.backgroundColor = NSColor.clearColor.CGColor;

    impl_->video_view =
        [[CrossDeskMetalVideoView alloc] initWithFrame:requested_view.frame];
    impl_->video_view.wantsLayer = YES;
    impl_->metal_layer =
        static_cast<CAMetalLayer*>(impl_->video_view.layer);
    impl_->metal_layer.device = impl_->device;
    impl_->metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    impl_->metal_layer.framebufferOnly = YES;
    impl_->metal_layer.opaque = YES;
    impl_->metal_layer.backgroundColor = NSColor.blackColor.CGColor;
    impl_->metal_layer.maximumDrawableCount = 3;
    impl_->metal_layer.displaySyncEnabled = YES;

    [requested_view.superview addSubview:impl_->video_view
                               positioned:NSWindowBelow
                               relativeTo:requested_view];
    impl_->title_accessory_controller =
        [[NSTitlebarAccessoryViewController alloc] init];
    impl_->title_accessory_controller.layoutAttribute =
        NSLayoutAttributeLeading;
    NSString* title = window.title.length > 0 ? window.title : @"CrossDesk";
    CrossDeskStreamTitleView* title_view =
        [[CrossDeskStreamTitleView alloc] initWithTitle:title];
    const auto surface_dirty_flag = impl_->native_surface_needs_redraw;
    title_view.surfaceInvalidationHandler = ^{
      surface_dirty_flag->store(true, std::memory_order_release);
    };
    impl_->title_accessory_controller.view = title_view;
    [window addTitlebarAccessoryViewController:
                impl_->title_accessory_controller];
    window.titleVisibility = NSWindowTitleHidden;

    // Adding the title accessory changes AppKit's content layout guide. Commit
    // it before sizing the video so a newly-created light window does not keep
    // the transparent full-size titlebar geometry from winit.
    NSView* frame_view = window.contentView.superview;
    frame_view.needsLayout = YES;
    [frame_view layoutSubtreeIfNeeded];
    impl_->RestoreVideoViewOrder();

    __weak NSTitlebarAccessoryViewController* title_accessory =
        impl_->title_accessory_controller;
    title_view.windowLayoutHandler = ^BOOL(NSWindow* window) {
      const bool fullscreen = IsStreamWindowFullscreen();
      title_accessory.view.hidden = fullscreen;
      if (fullscreen) {
        return YES;
      }
      const bool restored = ConfigureStreamWindowChrome(window);
      window.titleVisibility = NSWindowTitleHidden;
      return restored && window.titleVisibility == NSWindowTitleHidden;
    };
    impl_->UpdateVideoGeometry(0.0, true);
    impl_->native_surface_needs_redraw->store(true,
                                              std::memory_order_release);
    const bool attached = impl_->video_view.superview != nil;
    impl_->attached.store(attached, std::memory_order_release);
    return attached;
  }
}

void MacMetalVideoRenderer::Detach() {
  if (impl_) {
    impl_->DetachViews();
  }
}

bool MacMetalVideoRenderer::IsAttached() const {
  return impl_ && impl_->attached.load(std::memory_order_acquire);
}

bool MacMetalVideoRenderer::NeedsSurfaceRedraw() const {
  if (!IsReady() || !IsAttached()) {
    return false;
  }

  @autoreleasepool {
    NSWindow* window = impl_->slint_view.window;
    if (window == nil || !window.isVisible || window.isMiniaturized) {
      return false;
    }
  }

  return impl_->native_surface_needs_redraw->load(
      std::memory_order_acquire);
}

MacMetalVideoRenderer::RenderOutcome MacMetalVideoRenderer::RenderLatest(
    std::string_view remote_id, double top_inset_points,
    bool native_titlebar_visible) {
  if (!IsReady() || !IsAttached()) {
    return {RenderResult::failed};
  }

  @autoreleasepool {
    impl_->UpdateVideoGeometry(top_inset_points, native_titlebar_visible);
    if (impl_->metal_layer.drawableSize.width <= 0 ||
        impl_->metal_layer.drawableSize.height <= 0) {
      impl_->native_surface_needs_redraw->store(true,
                                                std::memory_order_release);
      return {RenderResult::idle};
    }

    const bool stream_changed = impl_->rendered_stream != remote_id;
    size_t slot_index = kFrameSlotCount;
    uint64_t sequence = 0;
    int source_width = 0;
    int source_height = 0;
    bool matching_frame_in_flight = false;
    uint64_t content_generation = 0;
    bool redraw_cached_frame = false;
    {
      std::lock_guard lock(impl_->frames->mutex);
      content_generation = impl_->frames->content_generation;
      redraw_cached_frame =
          stream_changed ||
          impl_->native_surface_needs_redraw->load(
              std::memory_order_acquire) ||
          impl_->rendered_content_generation != content_generation;
      for (size_t index = 0; index < impl_->frames->slots.size(); ++index) {
        const auto& slot = impl_->frames->slots[index];
        matching_frame_in_flight =
            matching_frame_in_flight ||
            (slot.valid && slot.use == SlotUse::in_flight &&
             slot.remote_id == remote_id);
        if (slot.valid && slot.use == SlotUse::pending &&
            slot.remote_id == remote_id &&
            (slot_index == kFrameSlotCount || slot.sequence > sequence)) {
          slot_index = index;
          sequence = slot.sequence;
          source_width = slot.width;
          source_height = slot.height;
        }
      }
      if (slot_index == kFrameSlotCount && redraw_cached_frame) {
        for (size_t index = 0; index < impl_->frames->slots.size(); ++index) {
          const auto& slot = impl_->frames->slots[index];
          if (slot.valid && slot.use == SlotUse::available &&
              slot.remote_id == remote_id &&
              (slot_index == kFrameSlotCount || slot.sequence > sequence)) {
            slot_index = index;
            sequence = slot.sequence;
            source_width = slot.width;
            source_height = slot.height;
          }
        }
      }
    }
    if (slot_index == kFrameSlotCount) {
      if (!redraw_cached_frame) {
        return {RenderResult::idle};
      }
      id<CAMetalDrawable> clear_drawable = [impl_->metal_layer nextDrawable];
      if (clear_drawable == nil) {
        return {impl_->MissingDrawableResult()};
      }
      MTLRenderPassDescriptor* clear_pass =
          [MTLRenderPassDescriptor renderPassDescriptor];
      clear_pass.colorAttachments[0].texture = clear_drawable.texture;
      clear_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      clear_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      clear_pass.colorAttachments[0].clearColor =
          MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
      id<MTLCommandBuffer> clear_command =
          [impl_->command_queue commandBuffer];
      id<MTLRenderCommandEncoder> clear_encoder =
          [clear_command renderCommandEncoderWithDescriptor:clear_pass];
      if (clear_command == nil || clear_encoder == nil) {
        impl_->native_surface_needs_redraw->store(true,
                                                  std::memory_order_release);
        return {RenderResult::failed};
      }
      [clear_encoder endEncoding];
      [clear_command presentDrawable:clear_drawable];
      [clear_command commit];
      impl_->rendered_stream.assign(remote_id);
      impl_->rendered_content_generation = content_generation;
      impl_->native_surface_needs_redraw->store(
          matching_frame_in_flight, std::memory_order_release);
      return {RenderResult::empty};
    }

    id<CAMetalDrawable> drawable = [impl_->metal_layer nextDrawable];
    if (drawable == nil) {
      return {impl_->MissingDrawableResult()};
    }

    id<MTLTexture> y_texture = nil;
    id<MTLTexture> uv_texture = nil;
    {
      std::lock_guard lock(impl_->frames->mutex);
      auto& slot = impl_->frames->slots[slot_index];
      if (!slot.valid ||
          (slot.use != SlotUse::pending &&
           slot.use != SlotUse::available) ||
          slot.sequence != sequence ||
          slot.remote_id != remote_id) {
        return {RenderResult::idle};
      }
      slot.use = SlotUse::in_flight;
      y_texture = slot.y_texture;
      uv_texture = slot.uv_texture;
    }

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [command_buffer renderCommandEncoderWithDescriptor:pass];
    if (command_buffer == nil || encoder == nil) {
      std::lock_guard lock(impl_->frames->mutex);
      auto& slot = impl_->frames->slots[slot_index];
      if (slot.sequence == sequence && slot.use == SlotUse::in_flight) {
        slot.use = SlotUse::available;
      }
      impl_->native_surface_needs_redraw->store(true,
                                                std::memory_order_release);
      return {RenderResult::failed};
    }

    const double target_width = drawable.texture.width;
    const double target_height = drawable.texture.height;
    const double source_aspect =
        static_cast<double>(source_width) / source_height;
    const double target_aspect = target_width / target_height;
    double viewport_width = target_width;
    double viewport_height = target_height;
    double viewport_x = 0.0;
    double viewport_y = 0.0;
    if (source_aspect > target_aspect) {
      viewport_height = target_width / source_aspect;
      viewport_y = (target_height - viewport_height) * 0.5;
    } else {
      viewport_width = target_height * source_aspect;
      viewport_x = (target_width - viewport_width) * 0.5;
    }

    [encoder setRenderPipelineState:impl_->pipeline];
    [encoder setViewport:MTLViewport{viewport_x, viewport_y, viewport_width,
                                     viewport_height, 0.0, 1.0}];
    [encoder setFragmentTexture:y_texture atIndex:0];
    [encoder setFragmentTexture:uv_texture atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0
                vertexCount:4];
    [encoder endEncoding];

    std::shared_ptr<SharedFrameState> completion_frames = impl_->frames;
    [command_buffer addCompletedHandler:^(id<MTLCommandBuffer>) {
      std::lock_guard lock(completion_frames->mutex);
      auto& slot = completion_frames->slots[slot_index];
      if (slot.sequence == sequence && slot.use == SlotUse::in_flight) {
        slot.use = SlotUse::available;
      }
    }];
    [command_buffer presentDrawable:drawable];
    [command_buffer commit];
    impl_->rendered_stream.assign(remote_id);
    impl_->rendered_content_generation = content_generation;
    impl_->native_surface_needs_redraw->store(false,
                                              std::memory_order_release);
    return {RenderResult::rendered, source_width, source_height, sequence};
  }
}

bool MacMetalVideoRenderer::CopyLatestNv12(
    std::string_view remote_id, std::vector<unsigned char>* output,
    int* width, int* height) const {
  if (!impl_ || output == nullptr || width == nullptr || height == nullptr) {
    return false;
  }

  std::lock_guard lock(impl_->frames->mutex);
  const FrameSlot* newest = nullptr;
  for (const auto& slot : impl_->frames->slots) {
    if (!slot.valid || slot.remote_id != remote_id || slot.y_buffer == nil ||
        slot.uv_buffer == nil || slot.sequence == 0) {
      continue;
    }
    if (newest == nullptr || slot.sequence > newest->sequence) {
      newest = &slot;
    }
  }
  if (newest == nullptr || newest->width <= 0 || newest->height <= 0) {
    return false;
  }

  const size_t y_size =
      static_cast<size_t>(newest->width) * newest->height;
  output->resize(y_size + y_size / 2);
  auto* y_source = static_cast<const uint8_t*>(newest->y_buffer.contents);
  auto* uv_source = static_cast<const uint8_t*>(newest->uv_buffer.contents);
  for (int row = 0; row < newest->height; ++row) {
    std::memcpy(output->data() + static_cast<size_t>(row) * newest->width,
                y_source + static_cast<size_t>(row) * newest->y_stride,
                newest->width);
  }
  uint8_t* uv_destination = output->data() + y_size;
  for (int row = 0; row < newest->height / 2; ++row) {
    std::memcpy(uv_destination + static_cast<size_t>(row) * newest->width,
                uv_source + static_cast<size_t>(row) * newest->uv_stride,
                newest->width);
  }
  *width = newest->width;
  *height = newest->height;
  return true;
}

}  // namespace crossdesk
