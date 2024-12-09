#ifndef _RTP_CODEC_H_
#define _RTP_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "fec_encoder.h"
#include "rtp_packet.h"

class RtpCodec {
 public:
  enum VideoFrameType {
    kEmptyFrame = 0,
    kVideoFrameKey = 3,
    kVideoFrameDelta = 4,
  };

 public:
  RtpCodec(RtpPacket::PAYLOAD_TYPE payload_type);
  ~RtpCodec();

 public:
  void Encode(uint8_t* buffer, uint32_t size, std::vector<RtpPacket>& packets);
  void Encode(VideoFrameType frame_type, uint8_t* buffer, uint32_t size,
              std::vector<RtpPacket>& packets);
  size_t Decode(RtpPacket& packet, uint8_t* payload);

  //  protected:
  //   void OnReceiveFrame(uint8_t* payload) = 0;
 private:
  bool IsKeyFrame(const uint8_t* buffer, uint32_t size);

  void EncodeAv1(uint8_t* buffer, uint32_t size,
                 std::vector<RtpPacket>& packets);

 private:
  uint8_t version_ = 0;
  bool has_padding_ = false;
  bool has_extension_ = false;
  uint32_t total_csrc_number_ = 0;
  bool marker_ = false;
  uint32_t payload_type_ = 0;
  uint16_t sequence_number_ = 0;
  uint64_t timestamp_ = 0;
  uint32_t ssrc_ = 0;
  std::vector<uint32_t> csrcs_;
  uint16_t profile_ = 0;
  uint16_t extension_profile_ = 0;
  uint16_t extension_len_ = 0;
  uint8_t* extension_data_ = nullptr;

 private:
  // RtpPacket* rtp_packet_ = nullptr;
  RtpPacket::FU_INDICATOR fu_indicator_;
  bool fec_enable_ = false;
  FecEncoder fec_encoder_;
};

class SSRCManager {
 public:
  static SSRCManager& Instance() {
    static SSRCManager instance;
    return instance;
  }

  void AddSsrc(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(mutex_);
    ssrcs_.insert(ssrc);
  }

  void DeleteSsrc(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(mutex_);
    ssrcs_.erase(ssrc);
  }

  bool Contains(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(mutex_);
    return ssrcs_.count(ssrc) > 0;
  }

 private:
  SSRCManager() = default;
  ~SSRCManager() = default;
  SSRCManager(const SSRCManager&) = delete;
  SSRCManager& operator=(const SSRCManager&) = delete;

  std::unordered_set<uint32_t> ssrcs_;
  std::mutex mutex_;
};

#endif