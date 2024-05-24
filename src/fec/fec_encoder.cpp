#include "fec_encoder.h"

#include "log.h"

FecEncoder::FecEncoder() {}

FecEncoder::~FecEncoder() {}

int FecEncoder::Init() {
  fec_codec_id_ = OF_CODEC_REED_SOLOMON_GF_2_M_STABLE;

  fec_rs_params_ = (of_rs_2_m_parameters_t *)calloc(1, sizeof(*fec_params_));
  if (nullptr == fec_rs_params_) {
    LOG_ERROR("Create FEC codec params failed");
    return -1;
  }

  fec_rs_params_->m = 8;
  fec_params_ = (of_parameters_t *)fec_rs_params_;

  if (OF_STATUS_OK !=
      of_create_codec_instance(&fec_session_, fec_codec_id_, OF_ENCODER, 2)) {
    LOG_ERROR("Create FEC codec instance failed");
    return -1;
  }

  return 0;
}

int FecEncoder::Release() {
  if (!fec_session_) {
    LOG_ERROR("Invalid FEC codec instance");
    return -1;
  }

  {
    if (OF_STATUS_OK != of_release_codec_instance(fec_session_)) {
      LOG_ERROR("Release FEC codec instance failed");
      return -1;
    }
  }

  if (fec_rs_params_) {
    free(fec_rs_params_);
  }

  return 0;
}

uint8_t **FecEncoder::Encode(const char *data, size_t len) {
  uint8_t **fec_packets = nullptr;

  unsigned int last_packet_size = len % max_size_of_packet_;
  unsigned int num_of_source_packets =
      len / max_size_of_packet_ + (last_packet_size ? 1 : 0);
  unsigned int num_of_total_packets =
      (unsigned int)floor((double)num_of_source_packets / code_rate_);

  fec_params_->nb_source_symbols = num_of_source_packets;
  fec_params_->nb_repair_symbols = num_of_total_packets - num_of_source_packets;

  fec_params_->encoding_symbol_length = max_size_of_packet_;

  if (OF_STATUS_OK != of_set_fec_parameters(fec_session_, fec_params_)) {
    LOG_ERROR("Set FEC params failed for codec_id {}", (int)fec_codec_id_);
    return nullptr;
  }

  fec_packets = (uint8_t **)calloc(num_of_total_packets, sizeof(uint8_t *));

  if (nullptr == fec_packets) {
    LOG_ERROR("Calloc failed for fec_packets with size [{}])",
              num_of_total_packets);
    return nullptr;
  }

  for (unsigned int esi = 0; esi < num_of_source_packets; esi++) {
    if (esi != num_of_source_packets - 1) {
      fec_packets[esi] =
          (uint8_t *)calloc(max_size_of_packet_, sizeof(uint8_t));
      if (nullptr == fec_packets[esi]) {
        LOG_ERROR("Calloc failed for fec_packets[{}] with size [{}])", esi,
                  max_size_of_packet_);
        ReleaseFecPackets(fec_packets, len);
        return nullptr;
      }
      memcpy(fec_packets[esi], data + esi * max_size_of_packet_,
             max_size_of_packet_);
    } else {
      fec_packets[esi] =
          (uint8_t *)calloc(max_size_of_packet_, sizeof(uint8_t));
      if (nullptr == fec_packets[esi]) {
        LOG_ERROR("Calloc failed for fec_packets[{}] with size [{}])", esi,
                  last_packet_size);
        ReleaseFecPackets(fec_packets, len);
        return nullptr;
      }
      memcpy(fec_packets[esi], data + esi * max_size_of_packet_,
             last_packet_size);
    }
  }

  for (unsigned int esi = num_of_source_packets; esi < num_of_total_packets;
       esi++) {
    fec_packets[esi] = (uint8_t *)calloc(max_size_of_packet_, sizeof(uint8_t));
    if (nullptr == fec_packets[esi]) {
      LOG_ERROR("Calloc failed for fec_packets[{}] with size [{}])", esi,
                max_size_of_packet_);
      ReleaseFecPackets(fec_packets, len);
      return nullptr;
    }
    if (OF_STATUS_OK !=
        of_build_repair_symbol(fec_session_, (void **)fec_packets, esi)) {
      LOG_ERROR("Build repair symbols failed for esi [{}]", esi);
      ReleaseFecPackets(fec_packets, len);
      return nullptr;
    }
  }

  return fec_packets;
}

int FecEncoder::ReleaseFecPackets(uint8_t **fec_packets, size_t len) {
  if (nullptr == fec_packets) {
    LOG_ERROR("Release Fec packets failed, due to fec_packets is nullptr");
    return -1;
  }
  unsigned int last_packet_size = len % max_size_of_packet_;
  unsigned int num_of_source_packets =
      len / max_size_of_packet_ + (last_packet_size ? 1 : 0);
  unsigned int num_of_total_packets =
      (unsigned int)floor((double)num_of_source_packets / code_rate_);

  for (unsigned int esi = 0; esi < num_of_total_packets; esi++) {
    if (fec_packets[esi]) {
      free(fec_packets[esi]);
    }
  }
  free(fec_packets);

  return 0;
}

void FecEncoder::GetFecPacketsParams(unsigned int source_length,
                                     unsigned int &num_of_total_packets,
                                     unsigned int &num_of_source_packets,
                                     unsigned int &last_packet_size) {
  last_packet_size = source_length % max_size_of_packet_;
  num_of_source_packets =
      source_length / max_size_of_packet_ + (last_packet_size ? 1 : 0);
  num_of_total_packets =
      (unsigned int)floor((double)num_of_source_packets / code_rate_);
}