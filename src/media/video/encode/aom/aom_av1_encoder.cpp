#include "aom_av1_encoder.h"

#include <chrono>
#include <cmath>

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
#define SAVE_H264_STREAM 1

#define YUV420P_BUFFER_SIZE 1280 * 720 * 3 / 2
static unsigned char yuv420p_buffer[YUV420P_BUFFER_SIZE];

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

constexpr uint8_t kObuSizePresentBit = 0b0'0000'010;

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
        error_code, param_id);
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

AomAv1Encoder::AomAv1Encoder() {}

AomAv1Encoder::~AomAv1Encoder() {
  if (SAVE_NV12_STREAM && file_nv12_) {
    fflush(file_nv12_);
    fclose(file_nv12_);
    file_nv12_ = nullptr;
  }

  if (SAVE_H264_STREAM && file_ivf_) {
    fflush(file_ivf_);
    fclose(file_ivf_);
    file_ivf_ = nullptr;
  }

  delete encoded_frame_;

  Release();
}

int AomAv1Encoder::Init() {
  encoded_frame_ = new uint8_t[YUV420P_BUFFER_SIZE];

  // Initialize encoder configuration structure with default values
  aom_codec_err_t ret = aom_codec_enc_config_default(
      aom_codec_av1_cx(), &aom_av1_encoder_config_, kUsageProfile);
  if (ret != AOM_CODEC_OK) {
    LOG_ERROR(
        "AomAv1Encoder::EncodeInit returned {} on aom_codec_enc_config_default",
        ret);
    return -1;
  }

  // Overwrite default config with input encoder settings & RTC-relevant values.
  aom_av1_encoder_config_.g_w = frame_width_;
  aom_av1_encoder_config_.g_h = frame_height_;
  aom_av1_encoder_config_.g_threads =
      NumberOfThreads(frame_width_, frame_height_, number_of_cores_);
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
              ret);
    return -1;
  }
  inited_ = true;

  // Set control parameters
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_CPUUSED, 4);
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

  if (SAVE_H264_STREAM) {
    file_ivf_ = fopen("encoded_stream.ivf", "w+b");
    if (!file_ivf_) {
      LOG_ERROR("Fail to open encoded_stream.ivf");
    }
  }

  if (SAVE_NV12_STREAM) {
    file_nv12_ = fopen("raw_stream.yuv", "w+b");
    if (!file_nv12_) {
      LOG_ERROR("Fail to open raw_stream.yuv");
    }
  }

  return 0;
}

int AomAv1Encoder::Encode(const uint8_t *pData, int nSize,
                          std::function<int(char *encoded_packets, size_t size,
                                            VideoFrameType frame_type)>
                              on_encoded_image) {
  if (SAVE_NV12_STREAM) {
    fwrite(pData, 1, nSize, file_nv12_);
  }

  const uint32_t duration =
      kRtpTicksPerSecond / static_cast<float>(max_frame_rate_);
  timestamp_ += duration;

  frame_for_encode_->planes[AOM_PLANE_Y] = const_cast<unsigned char *>(pData);
  frame_for_encode_->planes[AOM_PLANE_U] =
      const_cast<unsigned char *>(pData + frame_width_ * frame_height_);
  frame_for_encode_->planes[AOM_PLANE_V] = nullptr;
  frame_for_encode_->stride[AOM_PLANE_Y] = frame_width_;
  frame_for_encode_->stride[AOM_PLANE_U] = frame_width_;
  frame_for_encode_->stride[AOM_PLANE_V] = 0;

  // NV12ToYUV420PFFmpeg((unsigned char *)pData, frame_width_, frame_height_,
  //                     (unsigned char *)yuv420p_buffer);

  VideoFrameType frame_type;
  if (0 == seq_++ % 300) {
    force_i_frame_flags_ = AOM_EFLAG_FORCE_KF;
    frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    force_i_frame_flags_ = 0;
    frame_type = VideoFrameType::kVideoFrameDelta;
  }

  // Encode a frame. The presentation timestamp `pts` should not use real
  // timestamps from frames or the wall clock, as that can cause the rate
  // controller to misbehave.
  aom_codec_err_t ret =
      aom_codec_encode(&aom_av1_encoder_ctx_, frame_for_encode_, timestamp_,
                       duration, force_i_frame_flags_);
  if (ret != AOM_CODEC_OK) {
    LOG_ERROR("AomAv1Encoder::Encode returned {} on aom_codec_encode", ret);
    return -1;
  }

  aom_codec_iter_t iter = nullptr;
  int data_pkt_count = 0;
  while (const aom_codec_cx_pkt_t *pkt =
             aom_codec_get_cx_data(&aom_av1_encoder_ctx_, &iter)) {
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT && pkt->data.frame.sz > 0) {
      memcpy(encoded_frame_, pkt->data.frame.buf, pkt->data.frame.sz);
      encoded_frame_size_ = pkt->data.frame.sz;

      int qp = -1;
      SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_GET_LAST_QUANTIZER, &qp);
      LOG_INFO("Encoded frame qp = {}", qp);

      if (on_encoded_image) {
        on_encoded_image((char *)encoded_frame_, encoded_frame_size_,
                         frame_type);
        if (SAVE_H264_STREAM) {
          fwrite(encoded_frame_, 1, encoded_frame_size_, file_ivf_);
        }
      } else {
        OnEncodedImage((char *)encoded_frame_, encoded_frame_size_);
      }
    }
  }

  return 0;
}

int AomAv1Encoder::OnEncodedImage(char *encoded_packets, size_t size) {
  LOG_INFO("OnEncodedImage not implemented");
  return 0;
}

void AomAv1Encoder::ForceIdr() { force_i_frame_flags_ = AOM_EFLAG_FORCE_KF; }

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
