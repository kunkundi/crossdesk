#include "aom_av1_encoder.h"

#include <chrono>
#include <cmath>

#include "log.h"

// #define SAVE_RECEIVED_NV12_STREAM
// #define SAVE_ENCODED_AV1_STREAM

#define SET_ENCODER_PARAM_OR_RETURN_ERROR(param_id, param_value) \
  do {                                                           \
    if (!SetEncoderControlParameters(param_id, param_value)) {   \
      return -1;                                                 \
    }                                                            \
  } while (0)

constexpr int kQpMin = 10;
constexpr int kQpMax = 40;
constexpr int kUsageProfile = AOM_USAGE_REALTIME;
constexpr int kMinQindex = 145;  // Min qindex threshold for QP scaling.
constexpr int kMaxQindex = 205;  // Max qindex threshold for QP scaling.
constexpr int kBitDepth = 8;
constexpr int kLagInFrames = 0;  // No look ahead.
constexpr int kRtpTicksPerSecond = 90000;
constexpr double kMinimumFrameRate = 1.0;

static aom_superblock_size_t GetSuperblockSize(int width, int height,
                                               int threads) {
  int resolution = width * height;
  if (threads >= 4 && resolution >= 960 * 540 && resolution < 1920 * 1080)
    return AOM_SUPERBLOCK_SIZE_64X64;
  else
    return AOM_SUPERBLOCK_SIZE_DYNAMIC;
}

template <typename P>
bool AomAv1Encoder::SetEncoderControlParameters(int param_id, P param_value) {
  aom_codec_err_t error_code =
      aom_codec_control(&aom_av1_encoder_ctx_, param_id, param_value);
  if (error_code != AOM_CODEC_OK) {
    LOG_ERROR(
        "AomAv1Encoder::SetEncoderControlParameters returned {} on id: {}",
        (int)error_code, param_id);
  }
  return error_code == AOM_CODEC_OK;
}

int AomAv1Encoder::NumberOfThreads(int width, int height, int number_of_cores) {
  // Keep the number of encoder threads equal to the possible number of
  // column/row tiles, which is (1, 2, 4, 8). See comments below for
  // AV1E_SET_TILE_COLUMNS/ROWS.
  if (width * height > 1280 * 720 && number_of_cores > 8) {
    return 8;
  } else if (width * height >= 640 * 360 && number_of_cores > 4) {
    return 4;
  } else if (width * height >= 320 * 180 && number_of_cores > 2) {
    return 2;
  } else {
// Use 2 threads for low res on ARM.
#if defined(WEBRTC_ARCH_ARM) || defined(WEBRTC_ARCH_ARM64) || \
    defined(WEBRTC_ANDROID)
    if (width * height >= 320 * 180 && number_of_cores > 2) {
      return 2;
    }
#endif
    // 1 thread less than VGA.
    return 1;
  }
}

int AomAv1Encoder::GetCpuSpeed(int width, int height) {
  if (width * height <= 320 * 180)
    return 6;
  else if (width * height <= 640 * 360)
    return 7;
  else if (width * height <= 1280 * 720)
    return 8;
  else
    return 9;
}

int AomAv1Encoder::ResetEncodeResolution(unsigned int width,
                                         unsigned int height) {
  LOG_INFO("Reset encode resolution from [{}x{}] to [{}x{}]]", frame_width_,
           frame_height_, width, height);

  frame_width_ = width;
  frame_height_ = height;

  aom_av1_encoder_config_.g_w = width;
  aom_av1_encoder_config_.g_h = height;

  if (frame_for_encode_ != nullptr) {
    aom_img_free(frame_for_encode_);
    frame_for_encode_ = aom_img_wrap(nullptr, AOM_IMG_FMT_NV12, frame_width_,
                                     frame_height_, 1, nullptr);
  }

  return aom_codec_enc_config_set(&aom_av1_encoder_ctx_,
                                  &aom_av1_encoder_config_);
}

AomAv1Encoder::AomAv1Encoder(std::shared_ptr<SystemClock> clock)
    : clock_(clock) {}

AomAv1Encoder::~AomAv1Encoder() {
#ifdef SAVE_RECEIVED_NV12_STREAM
  if (file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }
#endif

#ifdef SAVE_ENCODED_AV1_STREAM
  if (file_av1_) {
    fflush(file_av1_);
    fclose(file_av1_);
    file_av1_ = nullptr;
  }
#endif

  delete[] encoded_frame_;
  encoded_frame_ = nullptr;

  Release();
}

int AomAv1Encoder::Init() {
  // Initialize encoder configuration structure with default values
  aom_codec_err_t ret = aom_codec_enc_config_default(
      aom_codec_av1_cx(), &aom_av1_encoder_config_, kUsageProfile);
  if (ret != AOM_CODEC_OK) {
    LOG_ERROR("Failed to get config: {}", aom_codec_err_to_string(ret));
    return -1;
  }

  // Overwrite default config with input encoder settings & RTC-relevant values.
  aom_av1_encoder_config_.g_w = frame_width_;
  aom_av1_encoder_config_.g_h = frame_height_;
  aom_av1_encoder_config_.g_threads = 8;
  aom_av1_encoder_config_.g_timebase.num = 1;
  aom_av1_encoder_config_.g_timebase.den = kRtpTicksPerSecond;
  aom_av1_encoder_config_.rc_target_bitrate = target_bitrate_;  // kilobits/sec.
  aom_av1_encoder_config_.rc_dropframe_thresh =
      (!disable_frame_dropping_) ? 30 : 0;
  aom_av1_encoder_config_.g_input_bit_depth = kBitDepth;
  aom_av1_encoder_config_.kf_mode = AOM_KF_DISABLED;
  aom_av1_encoder_config_.rc_min_quantizer = kQpMin;
  aom_av1_encoder_config_.rc_max_quantizer = kQpMax;
  aom_av1_encoder_config_.rc_undershoot_pct = 50;
  aom_av1_encoder_config_.rc_overshoot_pct = 50;
  aom_av1_encoder_config_.rc_buf_initial_sz = 600;
  aom_av1_encoder_config_.rc_buf_optimal_sz = 600;
  aom_av1_encoder_config_.rc_buf_sz = 1000;
  aom_av1_encoder_config_.g_usage = kUsageProfile;
  aom_av1_encoder_config_.g_error_resilient = 0;
  // Low-latency settings.
  aom_av1_encoder_config_.rc_end_usage = AOM_CBR;    // cbr mode
  aom_av1_encoder_config_.g_pass = AOM_RC_ONE_PASS;  // One-pass rate control
  aom_av1_encoder_config_.g_lag_in_frames = kLagInFrames;  // No look ahead

  if (frame_for_encode_ != nullptr) {
    aom_img_free(frame_for_encode_);
    frame_for_encode_ = nullptr;
  }

  // Flag options: AOM_CODEC_USE_PSNR and AOM_CODEC_USE_HIGHBITDEPTH
  aom_codec_flags_t flags = 0;

  // Initialize an encoder instance.
  ret = aom_codec_enc_init(&aom_av1_encoder_ctx_, aom_codec_av1_cx(),
                           &aom_av1_encoder_config_, flags);
  if (ret != AOM_CODEC_OK) {
    LOG_ERROR("AomAv1Encoder::EncodeInit returned {} on aom_codec_enc_init",
              (int)ret);
    return -1;
  }
  inited_ = true;

  // Set control parameters
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_CPUUSED, 10);  // 6 - 10
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_CDEF, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_TPL_MODEL, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_DELTAQ_MODE, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_ORDER_HINT, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_AQ_MODE, 3);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_MAX_INTRA_BITRATE_PCT, 300);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_COEFF_COST_UPD_FREQ, 3);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_MODE_COST_UPD_FREQ, 3);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_MV_COST_UPD_FREQ, 3);

  // if (codec_settings->mode == VideoCodecMode::kScreensharing) {
  //   SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_TUNE_CONTENT,
  //                                     AOM_CONTENT_SCREEN);
  //   SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_PALETTE, 1);
  // } else {
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_PALETTE, 0);
  // }

  if (aom_av1_encoder_config_.g_threads == 8) {
    // Values passed to AV1E_SET_TILE_ROWS and AV1E_SET_TILE_COLUMNS are log2()
    // based.
    // Use 4 tile columns x 2 tile rows for 8 threads.
    SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_TILE_ROWS, 1);
    SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_TILE_COLUMNS, 2);
  } else if (aom_av1_encoder_config_.g_threads == 4) {
    // Use 2 tile columns x 2 tile rows for 4 threads.
    SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_TILE_ROWS, 1);
    SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_TILE_COLUMNS, 1);
  } else {
    SET_ENCODER_PARAM_OR_RETURN_ERROR(
        AV1E_SET_TILE_COLUMNS,
        static_cast<int>(log2(aom_av1_encoder_config_.g_threads)));
  }

  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ROW_MT, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_OBMC, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_NOISE_SENSITIVITY, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_WARPED_MOTION, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_GLOBAL_MOTION, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_REF_FRAME_MVS, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(
      AV1E_SET_SUPERBLOCK_SIZE,
      GetSuperblockSize(frame_width_, frame_height_,
                        aom_av1_encoder_config_.g_threads));
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_CFL_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_SMOOTH_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_ANGLE_DELTA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_FILTER_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_INTRA_DEFAULT_TX_ONLY, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_DISABLE_TRELLIS_QUANT, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_DIST_WTD_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_DIFF_WTD_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_DUAL_FILTER, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTERINTRA_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTERINTRA_WEDGE, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTRA_EDGE_FILTER, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTRABC, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_MASKED_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_PAETH_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_QM, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_RECT_PARTITIONS, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_RESTORATION, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_SMOOTH_INTERINTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_TX64, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_MAX_REFERENCE_FRAMES, 3);

  frame_for_encode_ = aom_img_wrap(nullptr, AOM_IMG_FMT_NV12, frame_width_,
                                   frame_height_, 1, nullptr);

#ifdef SAVE_RECEIVED_NV12_STREAM
  file_nv12_ = fopen("received_nv12_stream.yuv", "w+b");
  if (!file_nv12_) {
    LOG_ERROR("Fail to open received_nv12_stream.yuv");
  }
#endif

#ifdef SAVE_ENCODED_AV1_STREAM
  file_av1_ = fopen("encoded_av1_stream.ivf", "w+b");
  if (!file_av1_) {
    LOG_ERROR("Fail to open encoded_av1_stream.ivf");
  }
#endif

  return 0;
}

int AomAv1Encoder::Encode(
    const XVideoFrame *video_frame,
    std::function<int(std::shared_ptr<EncodedFrame> encoded_frame)>
        on_encoded_image) {
#ifdef SAVE_RECEIVED_NV12_STREAM
  fwrite(video_frame->data, 1, video_frame->size, file_nv12_);
#endif

  aom_codec_err_t ret = AOM_CODEC_OK;

  if (!encoded_frame_) {
    encoded_frame_capacity_ = video_frame->size;
    encoded_frame_ = new uint8_t[encoded_frame_capacity_];
  }

  if (encoded_frame_capacity_ < video_frame->size) {
    encoded_frame_capacity_ = video_frame->size;
    delete[] encoded_frame_;
    encoded_frame_ = new uint8_t[encoded_frame_capacity_];
  }

  if (video_frame->width != frame_width_ ||
      video_frame->height != frame_height_) {
    if (AOM_CODEC_OK !=
        ResetEncodeResolution(video_frame->width, video_frame->height)) {
      LOG_ERROR("Reset encode resolution failed");
      return -1;
    }
  }

  const uint32_t duration =
      (uint32_t)(kRtpTicksPerSecond / static_cast<float>(max_frame_rate_));
  timestamp_ += duration;

  frame_for_encode_->planes[AOM_PLANE_Y] = (unsigned char *)(video_frame->data);
  frame_for_encode_->planes[AOM_PLANE_U] =
      (unsigned char *)(video_frame->data +
                        video_frame->width * video_frame->height);
  frame_for_encode_->planes[AOM_PLANE_V] = nullptr;
  frame_for_encode_->stride[AOM_PLANE_Y] = video_frame->width;
  frame_for_encode_->stride[AOM_PLANE_U] = video_frame->width;
  frame_for_encode_->stride[AOM_PLANE_V] = 0;

  VideoFrameType frame_type;
  if (0 == seq_++ % key_frame_interval_) {
    force_i_frame_flags_ = AOM_EFLAG_FORCE_KF;
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    force_i_frame_flags_ = 0;
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

  // Encode a frame. The presentation timestamp `pts` should not use real
  // timestamps from frames or the wall clock, as that can cause the rate
  // controller to misbehave.
  ret = aom_codec_encode(&aom_av1_encoder_ctx_, frame_for_encode_, timestamp_,
                         duration, force_i_frame_flags_);
  if (ret != AOM_CODEC_OK) {
    LOG_ERROR("Encode failed: {}, {}",
              aom_codec_error_detail(&aom_av1_encoder_ctx_),
              aom_codec_build_config());
    return -1;
  }

  aom_codec_iter_t iter = nullptr;
  while (const aom_codec_cx_pkt_t *pkt =
             aom_codec_get_cx_data(&aom_av1_encoder_ctx_, &iter)) {
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT && pkt->data.frame.sz > 0) {
      memcpy(encoded_frame_, pkt->data.frame.buf, pkt->data.frame.sz);
      encoded_frame_size_ = pkt->data.frame.sz;

      int qp = -1;
      SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_GET_LAST_QUANTIZER, &qp);
      // LOG_INFO("Encoded frame qp = {}", qp);

      if (on_encoded_image) {
        std::shared_ptr<EncodedFrame> encoded_frame =
            std::make_shared<EncodedFrame>(encoded_frame_, encoded_frame_size_,
                                           video_frame->width,
                                           video_frame->height);
        encoded_frame->SetFrameType(frame_type);
        encoded_frame->SetEncodedWidth(video_frame->width);
        encoded_frame->SetEncodedHeight(video_frame->height);
        encoded_frame->SetCapturedTimestamp(video_frame->captured_timestamp);
        encoded_frame->SetEncodedTimestamp(clock_->CurrentTime());
        on_encoded_image(encoded_frame);
#ifdef SAVE_ENCODED_AV1_STREAM
        fwrite(encoded_frame_, 1, encoded_frame_size_, file_av1_);
#endif
      }
    }
  }

  return 0;
}

int AomAv1Encoder::ForceIdr() {
  force_i_frame_flags_ = AOM_EFLAG_FORCE_KF;
  return 0;
}

int AomAv1Encoder::SetTargetBitrate(int bitrate) {
  target_bitrate_ = bitrate;
  return 0;
}

int AomAv1Encoder::Release() {
  if (frame_for_encode_ != nullptr) {
    aom_img_free(frame_for_encode_);
    frame_for_encode_ = nullptr;
  }
  if (inited_) {
    if (aom_codec_destroy(&aom_av1_encoder_ctx_)) {
      return -1;
    }
    inited_ = false;
  }

  return 0;
}
