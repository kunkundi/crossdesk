#include "openh264_encoder.h"

#include <chrono>

#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};
#ifdef __cplusplus
};
#endif

#define SAVE_NV12_STREAM 0
#define SAVE_H264_STREAM 0

#define YUV420P_BUFFER_SIZE 1280 * 720 * 3 / 2
static unsigned char yuv420p_buffer[YUV420P_BUFFER_SIZE];

static int NV12ToYUV420PFFmpeg(unsigned char *src_buffer, int width, int height,
                               unsigned char *dst_buffer) {
  AVFrame *Input_pFrame = av_frame_alloc();
  AVFrame *Output_pFrame = av_frame_alloc();
  struct SwsContext *img_convert_ctx = sws_getContext(
      width, height, AV_PIX_FMT_NV12, 1280, 720, AV_PIX_FMT_YUV420P,
      SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

  av_image_fill_arrays(Input_pFrame->data, Input_pFrame->linesize, src_buffer,
                       AV_PIX_FMT_NV12, width, height, 1);
  av_image_fill_arrays(Output_pFrame->data, Output_pFrame->linesize, dst_buffer,
                       AV_PIX_FMT_YUV420P, 1280, 720, 1);

  sws_scale(img_convert_ctx, (uint8_t const **)Input_pFrame->data,
            Input_pFrame->linesize, 0, height, Output_pFrame->data,
            Output_pFrame->linesize);

  if (Input_pFrame) av_free(Input_pFrame);
  if (Output_pFrame) av_free(Output_pFrame);
  if (img_convert_ctx) sws_freeContext(img_convert_ctx);

  return 0;
}

OpenH264Encoder::OpenH264Encoder() {}

OpenH264Encoder::~OpenH264Encoder() {
  if (SAVE_NV12_STREAM && file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }

  if (SAVE_H264_STREAM && file_h264_) {
    fflush(file_h264_);
    fclose(file_h264_);
    file_h264_ = nullptr;
  }
  delete encoded_frame_;
  Release();
}

SEncParamExt OpenH264Encoder::CreateEncoderParams() const {
  SEncParamExt encoder_params;
  openh264_encoder_->GetDefaultParams(&encoder_params);
  // if (codec_.mode == VideoCodecMode::kRealtimeVideo) {  //
  // encoder_params.iUsageType = CAMERA_VIDEO_REAL_TIME;
  // } else if (codec_.mode == VideoCodecMode::kScreensharing) {
  encoder_params.iUsageType = SCREEN_CONTENT_REAL_TIME;
  // }

  encoder_params.iPicWidth = frame_width_;
  encoder_params.iPicHeight = frame_height_;
  encoder_params.iTargetBitrate = target_bitrate_;
  encoder_params.iMaxBitrate = max_bitrate_;
  encoder_params.iRCMode = RC_BITRATE_MODE;
  encoder_params.fMaxFrameRate = 0.000030;
  encoder_params.bEnableFrameSkip = false;
  encoder_params.uiIntraPeriod = key_frame_interval_;
  encoder_params.uiMaxNalSize = 0;
  encoder_params.iMaxQp = 38;
  encoder_params.iMinQp = 16;
  // Threading model: use auto.
  //  0: auto (dynamic imp. internal encoder)
  //  1: single thread (default value)
  // >1: number of threads
  encoder_params.iMultipleThreadIdc = 1;
  // The base spatial layer 0 is the only one we use.
  encoder_params.sSpatialLayers[0].iVideoWidth = encoder_params.iPicWidth;
  encoder_params.sSpatialLayers[0].iVideoHeight = encoder_params.iPicHeight;
  encoder_params.sSpatialLayers[0].fFrameRate = encoder_params.fMaxFrameRate;
  encoder_params.sSpatialLayers[0].iSpatialBitrate =
      encoder_params.iTargetBitrate;
  encoder_params.sSpatialLayers[0].iMaxSpatialBitrate =
      encoder_params.iMaxBitrate;

  // SingleNalUnit
  encoder_params.sSpatialLayers[0].sSliceArgument.uiSliceNum = 1;
  encoder_params.sSpatialLayers[0].sSliceArgument.uiSliceMode =
      SM_SIZELIMITED_SLICE;
  encoder_params.sSpatialLayers[0].sSliceArgument.uiSliceSizeConstraint =
      static_cast<unsigned int>(max_payload_size_);
  LOG_INFO("Encoder is configured with NALU constraint: {} bytes",
           max_payload_size_);

  return encoder_params;
}

int OpenH264Encoder::Init() {
  // Create encoder.
  if (WelsCreateSVCEncoder(&openh264_encoder_) != 0) {
    LOG_ERROR("Failed to create OpenH264 encoder");
    return -1;
  }

  encoded_frame_ = new uint8_t[YUV420P_BUFFER_SIZE];

  int trace_level = WELS_LOG_QUIET;
  openh264_encoder_->SetOption(ENCODER_OPTION_TRACE_LEVEL, &trace_level);

  // Create encoder parameters based on the layer configuration.
  SEncParamExt encoder_params = CreateEncoderParams();

  if (openh264_encoder_->InitializeExt(&encoder_params) != 0) {
    LOG_ERROR("Failed to initialize OpenH264 encoder");
    // Release();
    return -1;
  }

  int video_format = EVideoFormatType::videoFormatI420;
  openh264_encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);

  if (SAVE_H264_STREAM) {
    file_h264_ = fopen("encoded_stream.h264", "w+b");
    if (!file_h264_) {
      LOG_WARN("Fail to open encoded_stream.h264");
    }
  }

  if (SAVE_NV12_STREAM) {
    file_nv12_ = fopen("raw_stream.yuv", "w+b");
    if (!file_nv12_) {
      LOG_WARN("Fail to open raw_stream.yuv");
    }
  }

  return 0;
}
int OpenH264Encoder::Encode(
    const uint8_t *pData, int nSize,
    std::function<int(char *encoded_packets, size_t size,
                      VideoFrameType frame_type)>
        on_encoded_image) {
  if (!openh264_encoder_) {
    LOG_ERROR("Invalid openh264 encoder");
    return -1;
  }

  if (SAVE_NV12_STREAM) {
    fwrite(yuv420p_buffer, 1, nSize, file_nv12_);
  }

  VideoFrameType frame_type;
  if (0 == seq_++ % 300) {
    ForceIdr();
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

  NV12ToYUV420PFFmpeg((unsigned char *)pData, frame_width_, frame_height_,
                      (unsigned char *)yuv420p_buffer);

  raw_frame_ = {0};
  raw_frame_.iPicWidth = frame_width_;
  raw_frame_.iPicHeight = frame_height_;
  raw_frame_.iColorFormat = EVideoFormatType::videoFormatI420;
  raw_frame_.uiTimeStamp =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();

  raw_frame_.iStride[0] = frame_width_;
  raw_frame_.iStride[1] = frame_width_ >> 1;
  raw_frame_.iStride[2] = frame_width_ >> 1;
  raw_frame_.pData[0] = (unsigned char *)yuv420p_buffer;
  raw_frame_.pData[1] = raw_frame_.pData[0] + frame_width_ * frame_height_;
  raw_frame_.pData[2] =
      raw_frame_.pData[1] + (frame_width_ * frame_height_ >> 2);

  SFrameBSInfo info;
  memset(&info, 0, sizeof(SFrameBSInfo));

  int enc_ret = openh264_encoder_->EncodeFrame(&raw_frame_, &info);
  if (enc_ret != 0) {
    LOG_ERROR("OpenH264 frame encoding failed, EncodeFrame returned {}",
              enc_ret);

    return -1;
  }

#if 1
  size_t required_capacity = 0;
  size_t fragments_count = 0;
  for (int layer = 0; layer < info.iLayerNum; ++layer) {
    const SLayerBSInfo &layerInfo = info.sLayerInfo[layer];
    for (int nal = 0; nal < layerInfo.iNalCount; ++nal, ++fragments_count) {
      required_capacity += layerInfo.pNalLengthInByte[nal];
    }
  }

  size_t frag = 0;
  int encoded_frame_size = 0;
  for (int layer = 0; layer < info.iLayerNum; ++layer) {
    const SLayerBSInfo &layerInfo = info.sLayerInfo[layer];
    size_t layer_len = 0;
    for (int nal = 0; nal < layerInfo.iNalCount; ++nal, ++frag) {
      layer_len += layerInfo.pNalLengthInByte[nal];
    }
    memcpy(encoded_frame_ + encoded_frame_size, layerInfo.pBsBuf, layer_len);
    encoded_frame_size += layer_len;
  }
  encoded_frame_size_ = encoded_frame_size;

  if (on_encoded_image) {
    on_encoded_image((char *)encoded_frame_, encoded_frame_size_, frame_type);
    if (SAVE_H264_STREAM) {
      fwrite(encoded_frame_, 1, encoded_frame_size_, file_h264_);
    }
  } else {
    OnEncodedImage((char *)encoded_frame_, encoded_frame_size_);
  }
#else
  if (info.eFrameType == videoFrameTypeInvalid) {
    LOG_ERROR("videoFrameTypeInvalid");
    return -1;
  }

  int temporal_id = 0;

  int encoded_frame_size = 0;

  if (info.eFrameType != videoFrameTypeSkip) {
    int layer = 0;
    while (layer < info.iLayerNum) {
      SLayerBSInfo *pLayerBsInfo = &(info.sLayerInfo[layer]);
      if (pLayerBsInfo != NULL) {
        int layer_size = 0;
        temporal_id = pLayerBsInfo->uiTemporalId;
        int nal_index = pLayerBsInfo->iNalCount - 1;
        do {
          layer_size += pLayerBsInfo->pNalLengthInByte[nal_index];
          --nal_index;
        } while (nal_index >= 0);
        memcpy(encoded_frame_ + encoded_frame_size, pLayerBsInfo->pBsBuf,
               layer_size);
        encoded_frame_size += layer_size;
      }
      ++layer;
    }

    got_output = true;

  } else {
    is_keyframe = false;
  }

  if (encoded_frame_size > 0) {
    encoded_frame_size_ = encoded_frame_size;

    if (on_encoded_image) {
      on_encoded_image((char *)encoded_frame_, frame_type);
      if (SAVE_H264_STREAM) {
        fwrite(encoded_frame_, 1, encoded_frame_size_, file_h264_);
      }
    } else {
      OnEncodedImage((char *)encoded_frame_, encoded_frame_size_);
    }

    EVideoFrameType ft_temp = info.eFrameType;
    if (ft_temp == 1 || ft_temp == 2) {
      is_keyframe = true;
    } else if (ft_temp == 3) {
      is_keyframe = false;
      if (temporal_) {
        if (temporal_id == 0 || temporal_id == 1) {
          is_keyframe = true;
        }
      }
    } else {
      is_keyframe = false;
    }
  }
#endif

  return 0;
}

int OpenH264Encoder::OnEncodedImage(char *encoded_packets, size_t size) {
  LOG_INFO("OnEncodedImage not implemented");
  return 0;
}

void OpenH264Encoder::ForceIdr() {
  if (openh264_encoder_) {
    openh264_encoder_->ForceIntraFrame(true);
  }
}

int OpenH264Encoder::Release() {
  if (openh264_encoder_) {
    WelsDestroySVCEncoder(openh264_encoder_);
  }

  return 0;
}
