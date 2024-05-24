#include "fec_decoder.h"

#include "log.h"

FecDecoder::FecDecoder() {}

FecDecoder::~FecDecoder() {}

int FecDecoder::Init() {
  fec_codec_id_ = OF_CODEC_REED_SOLOMON_GF_2_M_STABLE;

  fec_rs_params_ = (of_rs_2_m_parameters_t *)calloc(1, sizeof(*fec_params_));
  if (nullptr == fec_rs_params_) {
    LOG_ERROR("Create FEC decoder params failed");
    return -1;
  }

  fec_rs_params_->m = 8;
  fec_params_ = (of_parameters_t *)fec_rs_params_;

  if (OF_STATUS_OK !=
      of_create_codec_instance(&fec_session_, fec_codec_id_, OF_DECODER, 2)) {
    LOG_ERROR("Create FEC decoder instance failed");
    return -1;
  }

  return 0;
}

int FecDecoder::Release() {
  if (!fec_session_) {
    LOG_ERROR("Invalid FEC decoder instance");
    return -1;
  }

  {
    if (OF_STATUS_OK != of_release_codec_instance(fec_session_)) {
      LOG_ERROR("Release FEC decoder instance failed");
      return -1;
    }
  }

  if (fec_rs_params_) {
    free(fec_rs_params_);
  }

  return 0;
}

int FecDecoder::ResetParams(unsigned int source_symbol_num) {
  if (!fec_session_) {
    LOG_ERROR("Invalid FEC decoder instance");
    return -1;
  }

  num_of_received_symbols_ = 0;
  num_of_source_packets_ = source_symbol_num;
  num_of_total_packets_ =
      (unsigned int)floor((double)source_symbol_num / (double)code_rate_);

  LOG_ERROR("Set s[{}] r[{}]", num_of_source_packets_,
            num_of_total_packets_ - source_symbol_num);

  fec_params_->nb_source_symbols = source_symbol_num;
  fec_params_->nb_repair_symbols = num_of_total_packets_ - source_symbol_num;
  fec_params_->encoding_symbol_length = max_size_of_packet_;

  if (OF_STATUS_OK != of_set_fec_parameters(fec_session_, fec_params_)) {
    LOG_ERROR("Set FEC params failed for codec_id {}", (int)fec_codec_id_);
    return -1;
  }

  return 0;
}

uint8_t **FecDecoder::DecodeWithNewSymbol(const char *fec_symbol,
                                          unsigned int fec_symbol_id) {
  if (!fec_session_) {
    LOG_ERROR("Invalid FEC decoder instance");
    return nullptr;
  }

  num_of_received_symbols_++;
  if (OF_STATUS_ERROR == of_decode_with_new_symbol(
                             fec_session_, (char *)fec_symbol, fec_symbol_id)) {
    LOG_ERROR("Decode wit new symbol failed");
    return nullptr;
  }

  if ((num_of_received_symbols_ >= fec_params_->nb_source_symbols) &&
      (true == of_is_decoding_complete(fec_session_))) {
    uint8_t **source_packets =
        (uint8_t **)calloc(num_of_total_packets_, sizeof(uint8_t *));
    if (!source_packets) {
      LOG_ERROR("Calloc failed for source_packets with size [{}])",
                num_of_total_packets_);
    }

    if (OF_STATUS_OK !=
        of_get_source_symbols_tab(fec_session_, (void **)source_packets)) {
      LOG_ERROR("Get source symbols failed");
      return nullptr;
    }

    return source_packets;
  }

  return nullptr;
}

int FecDecoder::ReleaseSourcePackets(uint8_t **source_packets) {
  if (nullptr == source_packets) {
    LOG_ERROR(
        "Release source packets failed, due to source_packets is nullptr");
    return -1;
  }

  // for (unsigned int index = 0; index < num_of_source_packets_; index++) {
  //   if (source_packets[index]) {
  //     LOG_ERROR("Free [{}]", index);
  //     free(source_packets[index]);
  //   }
  // }
  free(source_packets);

  return 0;
}
