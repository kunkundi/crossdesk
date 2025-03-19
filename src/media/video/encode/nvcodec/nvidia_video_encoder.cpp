#include "nvidia_video_encoder.h"

#include <chrono>

#include "log.h"
#include "nvcodec_api.h"
#include "nvcodec_common.h"

// #define SAVE_RECEIVED_NV12_STREAM
// #define SAVE_ENCODED_H264_STREAM

NvidiaVideoEncoder::NvidiaVideoEncoder(std::shared_ptr<SystemClock> clock)
    : clock_(clock) {}
NvidiaVideoEncoder::~NvidiaVideoEncoder() {
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

  if (nv12_data_) {
    free(nv12_data_);
    nv12_data_ = nullptr;
  }

  if (encoder_) {
    encoder_->DestroyEncoder();
  }
}

int NvidiaVideoEncoder::Init() {
  ck(cuInit(0));
  int num_of_gpu = 0;
  ck(cuDeviceGetCount(&num_of_gpu));
  if (index_of_gpu_ < 0 || index_of_gpu_ >= num_of_gpu) {
    LOG_ERROR("GPU ordinal out of range. Should be within [0-{}]");
    return -1;
  }

  ck(cuDeviceGet(&cuda_device_, index_of_gpu_));
  char device_name[80];
  ck(cuDeviceGetName(device_name, sizeof(device_name), cuda_device_));
  LOG_INFO("H.264 encoder using [{}]", device_name);
  ck(cuCtxCreate(&cuda_context_, 0, cuda_device_));

  encoder_ = new NvEncoderCuda(cuda_context_, frame_width_, frame_height_,
                               buffer_format_, 0);

  NV_ENC_INITIALIZE_PARAMS init_params = {NV_ENC_INITIALIZE_PARAMS_VER};
  NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
  init_params.encodeConfig = &encodeConfig;
  encoder_->CreateDefaultEncoderParams(&init_params, codec_guid_, preset_guid_,
                                       tuning_info_);

  frame_width_max_ = encoder_->GetCapabilityValue(NV_ENC_CODEC_H264_GUID,
                                                  NV_ENC_CAPS_WIDTH_MAX);
  frame_height_max_ = encoder_->GetCapabilityValue(NV_ENC_CODEC_H264_GUID,
                                                   NV_ENC_CAPS_HEIGHT_MAX);
  // frame_width_min_ = encoder_->GetCapabilityValue(NV_ENC_CODEC_H264_GUID,
  //                                                 NV_ENC_CAPS_WIDTH_MIN);
  // frame_height_min_ = encoder_->GetCapabilityValue(NV_ENC_CODEC_H264_GUID,
  //                                                  NV_ENC_CAPS_HEIGHT_MIN);
  encode_level_max_ = encoder_->GetCapabilityValue(NV_ENC_CODEC_H264_GUID,
                                                   NV_ENC_CAPS_LEVEL_MAX);
  encode_level_min_ = encoder_->GetCapabilityValue(NV_ENC_CODEC_H264_GUID,
                                                   NV_ENC_CAPS_LEVEL_MIN);
  support_dynamic_resolution_ = encoder_->GetCapabilityValue(
      NV_ENC_CODEC_H264_GUID, NV_ENC_CAPS_SUPPORT_DYN_RES_CHANGE);
  support_dynamic_bitrate_ = encoder_->GetCapabilityValue(
      NV_ENC_CODEC_H264_GUID, NV_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE);

  init_params.encodeWidth = frame_width_;
  init_params.encodeHeight = frame_height_;
  // must set max encode width and height otherwise will get crash when try to
  // reconfigure the resolution
  init_params.maxEncodeWidth = frame_width_max_;
  init_params.maxEncodeHeight = frame_height_max_;
  // init_params.darWidth = init_params.encodeWidth;
  // init_params.darHeight = init_params.encodeHeight;

  encodeConfig.gopLength = key_frame_interval_;
  encodeConfig.frameIntervalP = 1;
  encodeConfig.encodeCodecConfig.h264Config.idrPeriod = key_frame_interval_;
  encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
  // encodeConfig.rcParams.enableMaxQP = 1;
  // encodeConfig.rcParams.enableMinQP = 1;
  // encodeConfig.rcParams.maxQP.qpIntra = 22;
  // encodeConfig.rcParams.minQP.qpIntra = 10;
  encodeConfig.rcParams.averageBitRate = average_bitrate_;
  // use the default VBV buffer size
  encodeConfig.rcParams.vbvBufferSize = 0;
  encodeConfig.rcParams.maxBitRate = max_bitrate_;
  // use the default VBV initial delay
  encodeConfig.rcParams.vbvInitialDelay = 0;
  // enable adaptive quantization (Spatial)
  encodeConfig.rcParams.enableAQ = false;
  encodeConfig.encodeCodecConfig.h264Config.idrPeriod = encodeConfig.gopLength;
  encodeConfig.encodeCodecConfig.h264Config.level = encode_level_max_;
  // encodeConfig.encodeCodecConfig.h264Config.disableSPSPPS = 1;
  // encodeConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;

  encoder_->CreateEncoder(&init_params);

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

int NvidiaVideoEncoder::Encode(
    const XVideoFrame *video_frame,
    std::function<int(std::shared_ptr<EncodedFrame> encoded_frame)>
        on_encoded_image) {
  if (!encoder_) {
    LOG_ERROR("Invalid encoder");
    return -1;
  }

#ifdef SAVE_RECEIVED_NV12_STREAM
  fwrite(video_frame->data, 1, video_frame->size, file_nv12_);
#endif

  if (video_frame->width != frame_width_ ||
      video_frame->height != frame_height_) {
    if (support_dynamic_resolution_) {
      if (0 != ResetEncodeResolution(video_frame->width, video_frame->height)) {
        return -1;
      }
    }
  }

  VideoFrameType frame_type;
  if (0 == seq_++ % key_frame_interval_) {
    ForceIdr();
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

#ifdef SHOW_SUBMODULE_TIME_COST
  auto start = std::chrono::steady_clock::now();
#endif

  const NvEncInputFrame *encoder_inputframe = encoder_->GetNextInputFrame();
  // LOG_ERROR("w:{}, h:{}", encoder_->GetEncodeWidth(),
  //           encoder_->GetEncodeHeight());
  NvEncoderCuda::CopyToDeviceFrame(
      cuda_context_,
      (void *)video_frame->data,  // NOLINT
      0, (CUdeviceptr)encoder_inputframe->inputPtr, encoder_inputframe->pitch,
      encoder_->GetEncodeWidth(), encoder_->GetEncodeHeight(),
      CU_MEMORYTYPE_HOST, encoder_inputframe->bufferFormat,
      encoder_inputframe->chromaOffsets, encoder_inputframe->numChromaPlanes);

  encoder_->EncodeFrame(encoded_packets_);

  if (encoded_packets_.size() < 1) {
    return -1;
  }

  for (const auto &packet : encoded_packets_) {
    if (on_encoded_image) {
      std::shared_ptr<EncodedFrame> encoded_frame =
          std::make_shared<EncodedFrame>(packet.data(), packet.size(),
                                         encoder_->GetEncodeWidth(),
                                         encoder_->GetEncodeHeight());
      encoded_frame->SetFrameType(frame_type);
      encoded_frame->SetEncodedWidth(encoder_->GetEncodeWidth());
      encoded_frame->SetEncodedHeight(encoder_->GetEncodeHeight());
      encoded_frame->SetCapturedTimestamp(video_frame->captured_timestamp);
      encoded_frame->SetEncodedTimestamp(clock_->CurrentTime());
      on_encoded_image(encoded_frame);
#ifdef SAVE_ENCODED_H264_STREAM
      fwrite((unsigned char *)packet.data(), 1, packet.size(), file_h264_);
#endif
    }
  }

#ifdef SHOW_SUBMODULE_TIME_COST
  auto encode_time_cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  LOG_INFO("Encode time cost {}ms", encode_time_cost);
#endif

  return 0;
}

int NvidiaVideoEncoder::ForceIdr() {
  if (!encoder_) {
    return -1;
  }

  NV_ENC_RECONFIGURE_PARAMS reconfig_params = {NV_ENC_RECONFIGURE_PARAMS_VER};
  NV_ENC_INITIALIZE_PARAMS init_params = {NV_ENC_INITIALIZE_PARAMS_VER};
  NV_ENC_CONFIG encode_config = {NV_ENC_CONFIG_VER};
  init_params.encodeConfig = &encode_config;
  encoder_->GetInitializeParams(&init_params);

  reconfig_params.reInitEncodeParams = init_params;
  reconfig_params.forceIDR = 1;

  if (!encoder_->Reconfigure(&reconfig_params)) {
    LOG_ERROR("Failed to force I frame");
    return -1;
  }

  return 0;
}

int NvidiaVideoEncoder::SetTargetBitrate(int bitrate) {
  if (!encoder_) {
    return -1;
  }

  NV_ENC_RECONFIGURE_PARAMS reconfig_params;
  reconfig_params.version = NV_ENC_RECONFIGURE_PARAMS_VER;
  NV_ENC_INITIALIZE_PARAMS init_params;
  NV_ENC_CONFIG encode_config = {NV_ENC_CONFIG_VER};
  init_params.encodeConfig = &encode_config;
  encoder_->GetInitializeParams(&init_params);
  init_params.frameRateDen = 1;
  init_params.frameRateNum = init_params.frameRateDen * fps_;
  init_params.encodeConfig->rcParams.averageBitRate = bitrate;
  init_params.encodeConfig->rcParams.maxBitRate = bitrate;
  reconfig_params.reInitEncodeParams = init_params;
  return encoder_->Reconfigure(&reconfig_params) ? 0 : -1;
}

int NvidiaVideoEncoder::ResetEncodeResolution(unsigned int width,
                                              unsigned int height) {
  if (!encoder_) {
    return -1;
  }

  if (width > frame_width_max_ || height > frame_height_max_) {
    LOG_ERROR(
        "Target resolution is too large for this hardware encoder, which "
        "[{}x{}] and support max resolution is [{}x{}]",
        width, height, frame_width_max_, frame_height_max_);
    return -1;
  }

  frame_width_ = width;
  frame_height_ = height;

  NV_ENC_RECONFIGURE_PARAMS reconfig_params = {NV_ENC_RECONFIGURE_PARAMS_VER};
  NV_ENC_INITIALIZE_PARAMS init_params = {NV_ENC_INITIALIZE_PARAMS_VER};
  NV_ENC_CONFIG encode_config = {NV_ENC_CONFIG_VER};
  init_params.encodeConfig = &encode_config;
  encoder_->GetInitializeParams(&init_params);

  reconfig_params.reInitEncodeParams = init_params;
  reconfig_params.reInitEncodeParams.encodeWidth = frame_width_;
  reconfig_params.reInitEncodeParams.encodeHeight = frame_height_;
  // reconfig_params.reInitEncodeParams.darWidth =
  //     reconfig_params.reInitEncodeParams.encodeWidth;
  // reconfig_params.reInitEncodeParams.darHeight =
  //     reconfig_params.reInitEncodeParams.encodeHeight;
  reconfig_params.forceIDR = 1;

  if (!encoder_->Reconfigure(&reconfig_params)) {
    LOG_ERROR("Failed to reset resolution");
    return -1;
  }

  return 0;
}