#include "screen_capturer_drm.h"

#if defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM && \
    defined(__has_include) && __has_include(<xf86drm.h>) && \
    __has_include(<xf86drmMode.h>)
#define CROSSDESK_DRM_BUILD_ENABLED 1
#include <xf86drm.h>
#include <xf86drmMode.h>
#elif defined(CROSSDESK_HAS_DRM) && CROSSDESK_HAS_DRM && \
    defined(__has_include) && __has_include(<libdrm/xf86drm.h>) && \
    __has_include(<libdrm/xf86drmMode.h>)
#define CROSSDESK_DRM_BUILD_ENABLED 1
#include <libdrm/xf86drm.h>
#include <libdrm/xf86drmMode.h>
#else
#define CROSSDESK_DRM_BUILD_ENABLED 0
#endif

#if CROSSDESK_DRM_BUILD_ENABLED

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include <display_stream_id.h>
#include "libyuv.h"
#include "rd_log.h"

namespace crossdesk {

namespace {

constexpr int kMaxDrmCards = 16;

const char* ConnectorTypeName(uint32_t type) {
  switch (type) {
    case DRM_MODE_CONNECTOR_VGA:
      return "VGA";
    case DRM_MODE_CONNECTOR_DVII:
      return "DVI-I";
    case DRM_MODE_CONNECTOR_DVID:
      return "DVI-D";
    case DRM_MODE_CONNECTOR_DVIA:
      return "DVI-A";
    case DRM_MODE_CONNECTOR_HDMIA:
      return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB:
      return "HDMI-B";
    case DRM_MODE_CONNECTOR_DisplayPort:
      return "DP";
    case DRM_MODE_CONNECTOR_eDP:
      return "eDP";
    case DRM_MODE_CONNECTOR_LVDS:
      return "LVDS";
#ifdef DRM_MODE_CONNECTOR_VIRTUAL
    case DRM_MODE_CONNECTOR_VIRTUAL:
      return "Virtual";
#endif
    default:
      return "Display";
  }
}

}  // namespace

ScreenCapturerDrm::ScreenCapturerDrm() {}

ScreenCapturerDrm::~ScreenCapturerDrm() { Destroy(); }

int ScreenCapturerDrm::Init(const int fps, cb_desktop_data cb) {
  Destroy();

  if (!cb) {
    LOG_ERROR("DRM screen capturer callback is null");
    return -1;
  }

  fps_ = std::max(1, fps);
  callback_ = cb;
  monitor_index_ = 0;
  initial_monitor_index_ = 0;
  consecutive_failures_ = 0;
  display_info_list_.clear();
  outputs_.clear();
  y_plane_.clear();
  uv_plane_.clear();

  if (!DiscoverOutputs()) {
    LOG_ERROR("DRM screen capturer could not find active outputs");
    callback_ = nullptr;
    CloseDevices();
    return -1;
  }

  return 0;
}

int ScreenCapturerDrm::Destroy() {
  Stop();
  callback_ = nullptr;
  display_info_list_.clear();
  outputs_.clear();
  y_plane_.clear();
  uv_plane_.clear();
  CloseDevices();
  return 0;
}

int ScreenCapturerDrm::Start(bool show_cursor) {
  if (running_) {
    return 0;
  }

  if (outputs_.empty()) {
    LOG_ERROR("DRM screen capturer has no output to capture");
    return -1;
  }

  show_cursor_ = show_cursor;
  paused_ = false;

  int probe_index = monitor_index_.load();
  if (probe_index < 0 || probe_index >= static_cast<int>(outputs_.size())) {
    probe_index = 0;
  }

  if (!CaptureOutputFrame(outputs_[probe_index], false)) {
    LOG_ERROR("DRM start probe failed on output {}", outputs_[probe_index].name);
    return -1;
  }

  running_ = true;
  thread_ = std::thread([this]() { CaptureLoop(); });
  return 0;
}

int ScreenCapturerDrm::Stop() {
  if (!running_) {
    return 0;
  }

  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  return 0;
}

int ScreenCapturerDrm::Pause([[maybe_unused]] int monitor_index) {
  paused_ = true;
  return 0;
}

int ScreenCapturerDrm::Resume([[maybe_unused]] int monitor_index) {
  paused_ = false;
  return 0;
}

int ScreenCapturerDrm::SwitchTo(int monitor_index) {
  if (monitor_index < 0 ||
      monitor_index >= static_cast<int>(display_info_list_.size())) {
    LOG_ERROR("Invalid DRM monitor index: {}", monitor_index);
    return -1;
  }

  monitor_index_ = monitor_index;
  return 0;
}

int ScreenCapturerDrm::ResetToInitialMonitor() {
  monitor_index_ = initial_monitor_index_;
  return 0;
}

std::vector<DisplayInfo> ScreenCapturerDrm::GetDisplayInfoList() {
  return display_info_list_;
}

bool ScreenCapturerDrm::DiscoverOutputs() {
  for (int card_index = 0; card_index < kMaxDrmCards; ++card_index) {
    const std::string card_path = "/dev/dri/card" + std::to_string(card_index);
    const int fd = open(card_path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }

    drmModeRes* resources = drmModeGetResources(fd);
    if (!resources) {
      close(fd);
      continue;
    }

    DrmDevice device;
    device.fd = fd;
    device.path = card_path;
    devices_.push_back(device);
    const int device_slot = static_cast<int>(devices_.size()) - 1;
    const size_t output_count_before = outputs_.size();

    for (int i = 0; i < resources->count_connectors; ++i) {
      drmModeConnector* connector =
          drmModeGetConnector(fd, resources->connectors[i]);
      if (!connector) {
        continue;
      }

      if (connector->connection != DRM_MODE_CONNECTED ||
          connector->count_modes <= 0) {
        drmModeFreeConnector(connector);
        continue;
      }

      uint32_t crtc_id = 0;
      if (connector->encoder_id != 0) {
        drmModeEncoder* encoder = drmModeGetEncoder(fd, connector->encoder_id);
        if (encoder) {
          crtc_id = encoder->crtc_id;
          drmModeFreeEncoder(encoder);
        }
      }

      if (crtc_id == 0) {
        for (int enc_idx = 0; enc_idx < connector->count_encoders; ++enc_idx) {
          drmModeEncoder* encoder =
              drmModeGetEncoder(fd, connector->encoders[enc_idx]);
          if (!encoder) {
            continue;
          }
          if (encoder->crtc_id != 0) {
            crtc_id = encoder->crtc_id;
            drmModeFreeEncoder(encoder);
            break;
          }
          drmModeFreeEncoder(encoder);
        }
      }

      if (crtc_id == 0) {
        drmModeFreeConnector(connector);
        continue;
      }

      drmModeCrtc* crtc = drmModeGetCrtc(fd, crtc_id);
      if (!crtc || !crtc->mode_valid || crtc->width <= 0 || crtc->height <= 0) {
        if (crtc) {
          drmModeFreeCrtc(crtc);
        }
        drmModeFreeConnector(connector);
        continue;
      }

      DrmOutput output;
      output.device_index = device_slot;
      output.connector_id = connector->connector_id;
      output.crtc_id = crtc_id;
      output.left = crtc->x;
      output.top = crtc->y;
      output.width = static_cast<int>(crtc->width);
      output.height = static_cast<int>(crtc->height);
      output.name = std::string(ConnectorTypeName(connector->connector_type)) +
                    std::to_string(connector->connector_type_id);
      output.stream_id = MakeDisplayStreamId(outputs_.size());

      outputs_.push_back(output);
      display_info_list_.push_back(
          DisplayInfo(output.name, output.left, output.top,
                      output.left + output.width, output.top + output.height));

      LOG_INFO("DRM output found: {} on {}, {}x{} @ ({}, {})", output.name,
               card_path, output.width, output.height, output.left, output.top);

      drmModeFreeCrtc(crtc);
      drmModeFreeConnector(connector);
    }

    drmModeFreeResources(resources);

    if (outputs_.size() == output_count_before) {
      close(fd);
      devices_.pop_back();
    }
  }

  if (outputs_.empty()) {
    return false;
  }

  LOG_INFO("DRM screen capturer discovered {} output(s)", outputs_.size());
  return true;
}

void ScreenCapturerDrm::CloseDevices() {
  for (auto& device : devices_) {
    if (device.fd >= 0) {
      close(device.fd);
      device.fd = -1;
    }
  }
  devices_.clear();
}

void ScreenCapturerDrm::CaptureLoop() {
  using clock = std::chrono::steady_clock;
  const auto frame_interval =
      std::chrono::milliseconds(std::max(1, 1000 / std::max(1, fps_)));

  while (running_) {
    const auto frame_start = clock::now();
    if (!paused_) {
      int index = monitor_index_.load();
      if (index >= 0 && index < static_cast<int>(outputs_.size())) {
        const bool ok = CaptureOutputFrame(outputs_[index], true);
        if (!ok) {
          ++consecutive_failures_;
          if (consecutive_failures_ == 1 || consecutive_failures_ % 60 == 0) {
            LOG_WARN("DRM capture failed (consecutive={})",
                     consecutive_failures_);
          }
        } else {
          consecutive_failures_ = 0;
        }
      }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now() - frame_start);
    if (elapsed < frame_interval) {
      std::this_thread::sleep_for(frame_interval - elapsed);
    }
  }
}

bool ScreenCapturerDrm::CaptureOutputFrame(const DrmOutput& output,
                                           bool emit_callback) {
  if (output.device_index < 0 ||
      output.device_index >= static_cast<int>(devices_.size())) {
    return false;
  }

  const int fd = devices_[output.device_index].fd;
  if (fd < 0) {
    return false;
  }

  drmModeCrtc* crtc = drmModeGetCrtc(fd, output.crtc_id);
  if (!crtc) {
    return false;
  }

  const uint32_t fb_id = crtc->buffer_id;
  drmModeFreeCrtc(crtc);
  if (fb_id == 0) {
    return false;
  }

  drmModeFB* fb = drmModeGetFB(fd, fb_id);
  if (!fb) {
    return false;
  }

  const uint32_t handle = fb->handle;
  const uint32_t pitch = fb->pitch;
  const int src_width = static_cast<int>(fb->width);
  const int src_height = static_cast<int>(fb->height);
  const int bpp = static_cast<int>(fb->bpp);
  drmModeFreeFB(fb);

  if (handle == 0 || pitch == 0 || src_width <= 1 || src_height <= 1) {
    return false;
  }

  if (bpp != 32) {
    LOG_WARN("DRM capture unsupported bpp: {}", bpp);
    return false;
  }

  const size_t map_size =
      static_cast<size_t>(pitch) * static_cast<size_t>(src_height);
  uint8_t* mapped_ptr = nullptr;
  size_t mapped_size = 0;
  int prime_fd = -1;
  if (!MapFramebuffer(fd, handle, map_size, &mapped_ptr, &mapped_size,
                      &prime_fd)) {
    return false;
  }

  int capture_width = std::min(src_width, output.width);
  int capture_height = std::min(src_height, output.height);
  if (capture_width <= 0 || capture_height <= 0) {
    capture_width = src_width;
    capture_height = src_height;
  }

  capture_width &= ~1;
  capture_height &= ~1;
  if (capture_width <= 1 || capture_height <= 1) {
    UnmapFramebuffer(mapped_ptr, mapped_size, prime_fd);
    return false;
  }

  const size_t y_size =
      static_cast<size_t>(capture_width) * static_cast<size_t>(capture_height);
  const size_t uv_size = y_size / 2;
  if (y_plane_.size() != y_size) {
    y_plane_.resize(y_size);
  }
  if (uv_plane_.size() != uv_size) {
    uv_plane_.resize(uv_size);
  }

  const int convert_ret =
      libyuv::ARGBToNV12(mapped_ptr, static_cast<int>(pitch), y_plane_.data(),
                         capture_width, uv_plane_.data(), capture_width,
                         capture_width, capture_height);

  if (convert_ret != 0) {
    UnmapFramebuffer(mapped_ptr, mapped_size, prime_fd);
    return false;
  }

  std::vector<uint8_t> nv12;
  nv12.reserve(y_plane_.size() + uv_plane_.size());
  nv12.insert(nv12.end(), y_plane_.begin(), y_plane_.end());
  nv12.insert(nv12.end(), uv_plane_.begin(), uv_plane_.end());

  if (emit_callback && callback_) {
    callback_(nv12.data(), static_cast<int>(nv12.size()), capture_width,
              capture_height, output.stream_id.c_str(), nullptr);
  }

  UnmapFramebuffer(mapped_ptr, mapped_size, prime_fd);
  return true;
}

bool ScreenCapturerDrm::MapFramebuffer(int fd, uint32_t handle, size_t map_size,
                                       uint8_t** mapped_ptr,
                                       size_t* mapped_size,
                                       int* prime_fd) const {
  if (!mapped_ptr || !mapped_size || !prime_fd || map_size == 0) {
    return false;
  }

  *mapped_ptr = nullptr;
  *mapped_size = 0;
  *prime_fd = -1;

  drm_mode_map_dumb map_arg{};
  map_arg.handle = handle;
  if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg) == 0) {
    void* mapped = mmap(nullptr, map_size, PROT_READ, MAP_SHARED, fd,
                        static_cast<off_t>(map_arg.offset));
    if (mapped != MAP_FAILED) {
      *mapped_ptr = static_cast<uint8_t*>(mapped);
      *mapped_size = map_size;
      return true;
    }
  }

  int dma_fd = -1;
  if (drmPrimeHandleToFD(fd, handle, DRM_CLOEXEC, &dma_fd) == 0) {
    size_t dma_map_size = map_size;
    const off_t fd_size = lseek(dma_fd, 0, SEEK_END);
    if (fd_size > 0) {
      dma_map_size = std::min(map_size, static_cast<size_t>(fd_size));
    }

    void* mapped =
        mmap(nullptr, dma_map_size, PROT_READ, MAP_SHARED, dma_fd, 0);
    if (mapped != MAP_FAILED) {
      *mapped_ptr = static_cast<uint8_t*>(mapped);
      *mapped_size = dma_map_size;
      *prime_fd = dma_fd;
      return true;
    }

    close(dma_fd);
  }

  return false;
}

void ScreenCapturerDrm::UnmapFramebuffer(uint8_t* mapped_ptr, size_t mapped_size,
                                         int prime_fd) const {
  if (mapped_ptr && mapped_size > 0) {
    munmap(mapped_ptr, mapped_size);
  }

  if (prime_fd >= 0) {
    close(prime_fd);
  }
}

}  // namespace crossdesk

#else

#include "rd_log.h"

namespace crossdesk {

ScreenCapturerDrm::ScreenCapturerDrm() {}

ScreenCapturerDrm::~ScreenCapturerDrm() { Destroy(); }

int ScreenCapturerDrm::Init([[maybe_unused]] const int fps, cb_desktop_data cb) {
  Destroy();
  callback_ = cb;
  LOG_WARN("DRM screen capturer disabled: libdrm headers not available");
  return -1;
}

int ScreenCapturerDrm::Destroy() {
  Stop();
  callback_ = nullptr;
  display_info_list_.clear();
  outputs_.clear();
  return 0;
}

int ScreenCapturerDrm::Start([[maybe_unused]] bool show_cursor) { return -1; }

int ScreenCapturerDrm::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  return 0;
}

int ScreenCapturerDrm::Pause([[maybe_unused]] int monitor_index) { return 0; }

int ScreenCapturerDrm::Resume([[maybe_unused]] int monitor_index) { return 0; }

int ScreenCapturerDrm::SwitchTo([[maybe_unused]] int monitor_index) {
  return -1;
}

int ScreenCapturerDrm::ResetToInitialMonitor() { return 0; }

std::vector<DisplayInfo> ScreenCapturerDrm::GetDisplayInfoList() {
  return display_info_list_;
}

bool ScreenCapturerDrm::DiscoverOutputs() { return false; }

void ScreenCapturerDrm::CloseDevices() {}

void ScreenCapturerDrm::CaptureLoop() {}

bool ScreenCapturerDrm::CaptureOutputFrame(
    [[maybe_unused]] const DrmOutput& output,
    [[maybe_unused]] bool emit_callback) {
  return false;
}

bool ScreenCapturerDrm::MapFramebuffer([[maybe_unused]] int fd,
                                       [[maybe_unused]] uint32_t handle,
                                       [[maybe_unused]] size_t map_size,
                                       [[maybe_unused]] uint8_t** mapped_ptr,
                                       [[maybe_unused]] size_t* mapped_size,
                                       [[maybe_unused]] int* prime_fd) const {
  return false;
}

void ScreenCapturerDrm::UnmapFramebuffer([[maybe_unused]] uint8_t* mapped_ptr,
                                         [[maybe_unused]] size_t mapped_size,
                                         [[maybe_unused]] int prime_fd) const {}

}  // namespace crossdesk

#endif
