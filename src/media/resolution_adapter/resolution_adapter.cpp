#include "resolution_adapter.h"

#include "libyuv.h"
#include "log.h"

int ResolutionAdapter::GetResolution(int target_bitrate, int current_width,
                                     int current_height, int* target_width,
                                     int* target_height) {
  if (target_bitrate < GetBitrateLimits().front().min_start_bitrate_bps) {
    *target_width = GetBitrateLimits().front().width;
    *target_height = GetBitrateLimits().front().height;
    return 0;
  }

  for (auto& resolution : GetBitrateLimits()) {
    if (target_bitrate >= resolution.min_start_bitrate_bps &&
        target_bitrate < resolution.max_bitrate_bps) {
      *target_width = resolution.width;
      *target_height = resolution.height;
      return 0;
    }
  }

  *target_width = -1;
  *target_height = -1;

  return -1;
}

int ResolutionAdapter::ResolutionDowngrade(const XVideoFrame* video_frame,
                                           int target_width, int target_height,
                                           XVideoFrame* new_frame) {
  if (target_width <= 0 || target_height <= 0) {
    return -1;
  }

  new_frame->width = target_width;
  new_frame->height = target_height;
  new_frame->data = new char[target_width * target_height * 3 / 2];

  libyuv::NV12Scale((const uint8_t*)(video_frame->data), video_frame->width,
                    (const uint8_t*)(video_frame->data +
                                     video_frame->width * video_frame->height),
                    video_frame->width, video_frame->width, video_frame->height,
                    (uint8_t*)(new_frame->data), target_width,
                    (uint8_t*)(new_frame->data + target_width * target_height),
                    target_width, target_width, target_height,
                    libyuv::kFilterLinear);

  return 0;
}