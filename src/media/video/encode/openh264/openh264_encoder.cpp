#include "openh264_encoder.h"

#include <chrono>

#include "libyuv.h"
#include "log.h"

#define SAVE_RECEIVED_NV12_STREAM 0
#define SAVE_ENCODED_H264_STREAM 0

#define YUV420P_BUFFER_SIZE 1280 * 720 * 3 / 2
static unsigned char yuv420p_buffer[YUV420P_BUFFER_SIZE];

void nv12ToI420(unsigned char *Src_data, int src_width, int src_height,
                unsigned char *Dst_data) {
  // NV12 video size
  int NV12_Size = src_width * src_height * 3 / 2;
  int NV12_Y_Size = src_width * src_height;

  // YUV420 video size
  int I420_Size = src_width * src_height * 3 / 2;
  int I420_Y_Size = src_width * src_height;
  int I420_U_Size = (src_width >> 1) * (src_height >> 1);
  int I420_V_Size = I420_U_Size;

  // src: buffer address of Y channel and UV channel
  unsigned char *Y_data_Src = Src_data;
  unsigned char *UV_data_Src = Src_data + NV12_Y_Size;
  int src_stride_y = src_width;
  int src_stride_uv = src_width;

  // dst: buffer address of Y channel、U channel and V channel
  unsigned char *Y_data_Dst = Dst_data;
  unsigned char *U_data_Dst = Dst_data + I420_Y_Size;
  unsigned char *V_data_Dst = Dst_data + I420_Y_Size + I420_U_Size;
  int Dst_Stride_Y = src_width;
  int Dst_Stride_U = src_width >> 1;
  int Dst_Stride_V = Dst_Stride_U;

  libyuv::NV12ToI420(
      (const uint8_t *)Y_data_Src, src_stride_y, (const uint8_t *)UV_data_Src,
      src_stride_uv, (uint8_t *)Y_data_Dst, Dst_Stride_Y, (uint8_t *)U_data_Dst,
      Dst_Stride_U, (uint8_t *)V_data_Dst, Dst_Stride_V, src_width, src_height);
}

OpenH264Encoder::OpenH264Encoder() {}

OpenH264Encoder::~OpenH264Encoder() {
  if (SAVE_RECEIVED_NV12_STREAM && file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }

  if (SAVE_ENCODED_H264_STREAM && file_h264_) {
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

  if (SAVE_RECEIVED_NV12_STREAM) {
    file_nv12_ = fopen("received_nv12_stream.yuv", "w+b");
    if (!file_nv12_) {
      LOG_WARN("Fail to open received_nv12_stream.yuv");
    }
  }

  if (SAVE_ENCODED_H264_STREAM) {
    file_h264_ = fopen("encoded_h264_stream.h264", "w+b");
    if (!file_h264_) {
      LOG_WARN("Fail to open encoded_h264_stream.h264");
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

  if (SAVE_RECEIVED_NV12_STREAM) {
    fwrite(pData, 1, nSize, file_nv12_);
  }

  VideoFrameType frame_type;
  if (0 == seq_++ % 300) {
    ForceIdr();
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

  nv12ToI420((unsigned char *)pData, frame_width_, frame_height_,
             yuv420p_buffer);

  raw_frame_ = {0};
  raw_frame_.iPicWidth = frame_width_;
  raw_frame_.iPicHeight = frame_height_;
  raw_frame_.iColorFormat = EVideoFormatType::videoFormatI420;
  raw_frame_.uiTimeStamp =
      std::chrono::system_clock::now().time_since_epoch().count();

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
    if (SAVE_ENCODED_H264_STREAM) {
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
      if (SAVE_ENCODED_H264_STREAM) {
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
