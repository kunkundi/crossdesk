#include "nvidia_video_encoder.h"

#include <chrono>

#include "log.h"

#define SAVE_ENCODER_STREAM 0

NvidiaVideoEncoder::NvidiaVideoEncoder() {}
NvidiaVideoEncoder::~NvidiaVideoEncoder() {
  if (SAVE_ENCODER_STREAM && file_) {
    fflush(file_);
    fclose(file_);
    file_ = nullptr;
  }

  if (nv12_data_) {
    free(nv12_data_);
    nv12_data_ = nullptr;
  }
}

int NvidiaVideoEncoder::Init() {
  // Init cuda context
  int num_of_GPUs = 0;
  CUdevice cuda_device;
  bool cuda_ctx_succeed =
      (index_of_GPU >= 0 && cuInit(0) == CUresult::CUDA_SUCCESS &&
       cuDeviceGetCount(&num_of_GPUs) == CUresult::CUDA_SUCCESS &&
       (num_of_GPUs > 0 && index_of_GPU < num_of_GPUs) &&
       cuDeviceGet(&cuda_device, index_of_GPU) == CUresult::CUDA_SUCCESS &&
       cuCtxCreate(&cuda_context_, 0, cuda_device) == CUresult::CUDA_SUCCESS);
  if (!cuda_ctx_succeed) {
  }

  encoder_ = new NvEncoderCuda(cuda_context_, frame_width_, frame_height_,
                               NV_ENC_BUFFER_FORMAT::NV_ENC_BUFFER_FORMAT_NV12);

  // Init encoder_ session
  NV_ENC_INITIALIZE_PARAMS init_params;
  init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
  NV_ENC_CONFIG encode_config = {NV_ENC_CONFIG_VER};
  init_params.encodeConfig = &encode_config;

  encoder_->CreateDefaultEncoderParams(&init_params, codec_guid, preset_guid,
                                       tuning_info);

  init_params.encodeWidth = frame_width_;
  init_params.encodeHeight = frame_height_;
  init_params.encodeConfig->profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
  init_params.encodeConfig->gopLength = keyFrameInterval_;
  init_params.encodeConfig->frameIntervalP = 1;
  init_params.encodeConfig->rcParams.rateControlMode =
      NV_ENC_PARAMS_RC_MODE::NV_ENC_PARAMS_RC_VBR;
  init_params.encodeConfig->rcParams.maxBitRate = maxBitrate_ * 500;
  // init_params.encodeConfig->rcParams.enableMinQP = 1;
  // init_params.encodeConfig->rcParams.minQP.qpIntra = 10;
  init_params.encodeConfig->rcParams.enableMaxQP = 1;
  init_params.encodeConfig->rcParams.maxQP.qpIntra = 22;
  init_params.encodeConfig->encodeCodecConfig.h264Config.level =
      NV_ENC_LEVEL::NV_ENC_LEVEL_H264_31;
  init_params.encodeConfig->encodeCodecConfig.h264Config.sliceMode = 1;
  init_params.encodeConfig->encodeCodecConfig.h264Config.sliceModeData =
      max_payload_size_;
  // init_params.encodeConfig->encodeCodecConfig.h264Config.disableSPSPPS = 1;
  // init_params.encodeConfig->encodeCodecConfig.h264Config.repeatSPSPPS = 1;

  encoder_->CreateEncoder(&init_params);

  if (SAVE_ENCODER_STREAM) {
    file_ = fopen("encode_stream.h264", "w+b");
    if (!file_) {
      LOG_WARN("Fail to open stream.h264");
    }
  }
  return 0;
}

int NvidiaVideoEncoder::Encode(
    const uint8_t *pData, int nSize,
    std::function<int(char *encoded_packets, size_t size,
                      VideoFrameType frame_type)>
        on_encoded_image) {
  if (!encoder_) {
    LOG_ERROR("Invalid encoder");
    return -1;
  }

  VideoFrameType frame_type;
  if (0 == seq_++ % 300) {
    ForceIdr();
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

#ifdef SHOW_SUBMODULE_TIME_COST
  auto start = std::chrono::steady_clock::now();
#endif

  const NvEncInputFrame *encoder_inputframe = encoder_->GetNextInputFrame();

  NvEncoderCuda::CopyToDeviceFrame(
      cuda_context_,
      (void *)pData,  // NOLINT
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
      on_encoded_image((char *)packet.data(), packet.size(), frame_type);
      if (SAVE_ENCODER_STREAM) {
        fwrite(packet.data(), 1, packet.size(), file_);
      }
    } else {
      OnEncodedImage((char *)packet.data(), packet.size());
    }

    if (SAVE_ENCODER_STREAM) {
      fwrite((unsigned char *)packet.data(), 1, packet.size(), file_);
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

int NvidiaVideoEncoder::OnEncodedImage(char *encoded_packets, size_t size) {
  LOG_INFO("OnEncodedImage not implemented");
  return 0;
}

void NvidiaVideoEncoder::ForceIdr() {
  NV_ENC_RECONFIGURE_PARAMS reconfig_params;
  reconfig_params.version = NV_ENC_RECONFIGURE_PARAMS_VER;

  NV_ENC_INITIALIZE_PARAMS init_params;
  NV_ENC_CONFIG encode_config = {NV_ENC_CONFIG_VER};
  init_params.encodeConfig = &encode_config;
  encoder_->GetInitializeParams(&init_params);

  reconfig_params.reInitEncodeParams = init_params;
  reconfig_params.forceIDR = 1;
  reconfig_params.resetEncoder = 1;

  if (!encoder_->Reconfigure(&reconfig_params)) {
    LOG_ERROR("Failed to force I frame");
  }
}
