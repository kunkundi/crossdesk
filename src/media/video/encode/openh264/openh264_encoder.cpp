#include "openh264_encoder.h"

#include <chrono>

#include "libyuv.h"
#include "log.h"

// #define SAVE_RECEIVED_NV12_STREAM
// #define SAVE_ENCODED_H264_STREAM

void Nv12ToI420(unsigned char *Src_data, int src_width, int src_height,
                unsigned char *Dst_data) {
  // NV12
  int NV12_Y_Size = src_width * src_height;

  // YUV420
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
  unsigned char *V_data_Dst = Dst_data + I420_Y_Size + I420_V_Size;
  int Dst_Stride_Y = src_width;
  int Dst_Stride_U = src_width >> 1;
  int Dst_Stride_V = Dst_Stride_U;

  libyuv::NV12ToI420(
      (const uint8_t *)Y_data_Src, src_stride_y, (const uint8_t *)UV_data_Src,
      src_stride_uv, (uint8_t *)Y_data_Dst, Dst_Stride_Y, (uint8_t *)U_data_Dst,
      Dst_Stride_U, (uint8_t *)V_data_Dst, Dst_Stride_V, src_width, src_height);
}

OpenH264Encoder::OpenH264Encoder(std::shared_ptr<SystemClock> clock)
    : clock_(clock) {}

OpenH264Encoder::~OpenH264Encoder() {
#ifdef SAVE_RECEIVED_NV12_STREAM
  if (file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }
#endif

#ifdef SAVE_ENCODED_H264_STREAM
  if (file_h264_) {
    fflush(file_h264_);
    fclose(file_h264_);
    file_h264_ = nullptr;
  }
#endif

  if (yuv420p_frame_) {
    delete[] yuv420p_frame_;
    yuv420p_frame_ = nullptr;
  }

  if (encoded_frame_) {
    delete[] encoded_frame_;
    encoded_frame_ = nullptr;
  }

  Release();
}

int OpenH264Encoder::InitEncoderParams(int width, int height) {
  int ret = 0;

  if (!openh264_encoder_) {
    LOG_ERROR("Invalid openh264 encoder");
    return -1;
  }

  ret = openh264_encoder_->GetDefaultParams(&encoder_params_);
  // if (codec_.mode == VideoCodecMode::kRealtimeVideo) {  //
  encoder_params_.iUsageType = CAMERA_VIDEO_REAL_TIME;
  // } else if (codec_.mode == VideoCodecMode::kScreensharing) {
  // encoder_params_.iUsageType = SCREEN_CONTENT_REAL_TIME;
  // }

  encoder_params_.iPicWidth = width;
  encoder_params_.iPicHeight = height;
  encoder_params_.iTargetBitrate = target_bitrate_;
  encoder_params_.iMaxBitrate = max_bitrate_;
  encoder_params_.iRCMode = RC_BITRATE_MODE;
  encoder_params_.fMaxFrameRate = 60;
  encoder_params_.bEnableFrameSkip = false;
  encoder_params_.uiIntraPeriod = key_frame_interval_;
  encoder_params_.uiMaxNalSize = 0;
  encoder_params_.iMaxQp = 38;
  encoder_params_.iMinQp = 16;
  // Threading model: use auto.
  //  0: auto (dynamic imp. internal encoder)
  //  1: single thread (default value)
  // >1: number of threads
  encoder_params_.iMultipleThreadIdc = 8;
  // The base spatial layer 0 is the only one we use.
  encoder_params_.sSpatialLayers[0].iVideoWidth = encoder_params_.iPicWidth;
  encoder_params_.sSpatialLayers[0].iVideoHeight = encoder_params_.iPicHeight;
  encoder_params_.sSpatialLayers[0].fFrameRate = encoder_params_.fMaxFrameRate;
  encoder_params_.sSpatialLayers[0].iSpatialBitrate =
      encoder_params_.iTargetBitrate;
  encoder_params_.sSpatialLayers[0].iMaxSpatialBitrate =
      encoder_params_.iMaxBitrate;

  // SingleNalUnit
  encoder_params_.sSpatialLayers[0].sSliceArgument.uiSliceNum = 1;
  encoder_params_.sSpatialLayers[0].sSliceArgument.uiSliceMode =
      SM_SIZELIMITED_SLICE;
  encoder_params_.sSpatialLayers[0].sSliceArgument.uiSliceSizeConstraint =
      static_cast<unsigned int>(max_payload_size_);
  LOG_INFO("Encoder is configured with NALU constraint: {} bytes",
           max_payload_size_);

  return ret;
}

int OpenH264Encoder::ResetEncodeResolution(unsigned int width,
                                           unsigned int height) {
  frame_width_ = width;
  frame_height_ = height;

  if (0 != InitEncoderParams(width, height)) {
    LOG_ERROR("Reset encoder params [{}x{}] failed", width, height);
    return -1;
  }

  if (0 != openh264_encoder_->InitializeExt(&encoder_params_)) {
    LOG_ERROR("Reset encoder resolution [{}x{}] failed", width, height);
    return -1;
  }

  return 0;
}

int OpenH264Encoder::Init() {
  // Create encoder.
  if (WelsCreateSVCEncoder(&openh264_encoder_) != 0) {
    LOG_ERROR("Failed to create OpenH264 encoder");
    return -1;
  }

  int trace_level = WELS_LOG_QUIET;
  openh264_encoder_->SetOption(ENCODER_OPTION_TRACE_LEVEL, &trace_level);

  // Create encoder parameters based on the layer configuration.
  InitEncoderParams(frame_width_, frame_height_);

  if (openh264_encoder_->InitializeExt(&encoder_params_) != 0) {
    LOG_ERROR("Failed to initialize OpenH264 encoder");
    // Release();
    return -1;
  }

  video_format_ = EVideoFormatType::videoFormatI420;
  openh264_encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format_);

#ifdef SAVE_RECEIVED_NV12_STREAM
  file_nv12_ = fopen("received_nv12_stream.yuv", "w+b");
  if (!file_nv12_) {
    LOG_WARN("Fail to open received_nv12_stream.yuv");
  }
#endif

#ifdef SAVE_ENCODED_H264_STREAM
  file_h264_ = fopen("encoded_h264_stream.h264", "w+b");
  if (!file_h264_) {
    LOG_WARN("Fail to open encoded_h264_stream.h264");
  }
#endif

  return 0;
}

int OpenH264Encoder::Encode(
    const XVideoFrame *video_frame,
    std::function<int(std::shared_ptr<EncodedFrame> encoded_frame)>
        on_encoded_image) {
  if (!openh264_encoder_) {
    LOG_ERROR("Invalid openh264 encoder");
    return -1;
  }

#ifdef SAVE_RECEIVED_NV12_STREAM
  fwrite(video_frame->data, 1, video_frame->size, file_nv12_);
#endif

  if (!yuv420p_frame_) {
    yuv420p_frame_capacity_ = video_frame->size;
    yuv420p_frame_ = new unsigned char[yuv420p_frame_capacity_];
  }

  if (yuv420p_frame_capacity_ < video_frame->size) {
    yuv420p_frame_capacity_ = video_frame->size;
    delete[] yuv420p_frame_;
    yuv420p_frame_ = new unsigned char[yuv420p_frame_capacity_];
  }

  if (!encoded_frame_) {
    encoded_frame_capacity_ = video_frame->size;
    encoded_frame_ = new unsigned char[encoded_frame_capacity_];
  }

  if (encoded_frame_capacity_ < video_frame->size) {
    encoded_frame_capacity_ = video_frame->size;
    delete[] encoded_frame_;
    encoded_frame_ = new unsigned char[encoded_frame_capacity_];
  }

  if (video_frame->width != frame_width_ ||
      video_frame->height != frame_height_) {
    ResetEncodeResolution(video_frame->width, video_frame->height);
  }

  Nv12ToI420((unsigned char *)video_frame->data, video_frame->width,
             video_frame->height, yuv420p_frame_);

  VideoFrameType frame_type;
  if (0 == seq_++ % key_frame_interval_) {
    ForceIdr();
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

  raw_frame_ = {0};
  raw_frame_.iPicWidth = video_frame->width;
  raw_frame_.iPicHeight = video_frame->height;
  raw_frame_.iColorFormat = video_format_;
  raw_frame_.uiTimeStamp =
      std::chrono::system_clock::now().time_since_epoch().count();

  raw_frame_.iStride[0] = video_frame->width;
  raw_frame_.iStride[1] = video_frame->width >> 1;
  raw_frame_.iStride[2] = video_frame->width >> 1;
  raw_frame_.pData[0] = (unsigned char *)yuv420p_frame_;
  raw_frame_.pData[1] =
      raw_frame_.pData[0] + video_frame->width * video_frame->height;
  raw_frame_.pData[2] =
      raw_frame_.pData[1] + (video_frame->width * video_frame->height >> 2);

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
  size_t encoded_frame_size = 0;
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
    std::shared_ptr<EncodedFrame> encoded_frame =
        std::make_shared<EncodedFrame>(encoded_frame_, encoded_frame_size_,
                                       raw_frame_.iPicWidth,
                                       raw_frame_.iPicHeight);
    encoded_frame->SetFrameType(frame_type);
    encoded_frame->SetEncodedWidth(raw_frame_.iPicWidth);
    encoded_frame->SetEncodedHeight(raw_frame_.iPicHeight);
    encoded_frame->SetCapturedTimestamp(video_frame->captured_timestamp);
    encoded_frame->SetEncodedTimestamp(clock_->CurrentTime());
    on_encoded_image(encoded_frame);
#ifdef SAVE_ENCODED_H264_STREAM
    fwrite(encoded_frame_, 1, encoded_frame_size_, file_h264_);
#endif
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
      encoded_frame->SetFrameType(frame_type);
      encoded_frame->SetEncodedWidth(raw_frame_.iPicWidth);
      encoded_frame->SetEncodedHeight(raw_frame_.iPicHeight);
      encoded_frame->SetCapturedTimestamp(video_frame->captured_timestamp);
      encoded_frame->SetEncodedTimestamp(clock_->CurrentTime());
      on_encoded_image((char *)encoded_frame_, frame_type);
#ifdef SAVE_ENCODED_H264_STREAM
      fwrite(encoded_frame_, 1, encoded_frame_size_, file_h264_);
#endif
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

int OpenH264Encoder::ForceIdr() {
  if (openh264_encoder_) {
    return openh264_encoder_->ForceIntraFrame(true);
  }

  return 0;
}

int OpenH264Encoder::SetTargetBitrate(int bitrate) {
  if (!openh264_encoder_) {
    return -1;
  }

  target_bitrate_ = bitrate;
  encoder_params_.iTargetBitrate = target_bitrate_;

  return openh264_encoder_->SetOption(ENCODER_OPTION_BITRATE, &target_bitrate_);
}

int OpenH264Encoder::Release() {
  if (openh264_encoder_) {
    openh264_encoder_->Uninitialize();
    WelsDestroySVCEncoder(openh264_encoder_);
  }

  return 0;
}
