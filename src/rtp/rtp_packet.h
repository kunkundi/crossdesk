#ifndef _RTP_PACKET_H_
#define _RTP_PACKET_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <vector>

// Common
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |       defined by profile      |            length             |x
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                           Payload                             |
// |             ....              :  padding...                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// H264
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |       defined by profile      |            length             |x
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |  FU indicator |   FU header   |                               |
// |                                                               |
// |                           FU Payload                          |
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// |  FU indicator |   FU header   |
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |F|NRI|   Type  |S|E|R|   Type  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// H264 FEC source symbol
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |       defined by profile      |            length             |x
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |  FEC symbol id |  src sym num  | FU indicator |   FU header   |
// |                                                               |
// |                           FU Payload                          |
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// H264 FEC repair symbol
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |       defined by profile      |            length             |x
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// | FEC symbol id |  src sym num  |                               |
// |                                                               |
// |                          Fec Payload                          |
// |                                                               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// AV1
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            contributing source (CSRC) identifiers             |x
// |                             ....                              |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |         0x100         |  0x0  |       extensions length       |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |      ID       |  hdr_length   |                               |x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+                               |x
// |                                                               |x
// |          dependency descriptor (hdr_length #bytes)            |x
// |                                                               |x
// |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                               | Other rtp header extensions...|x
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// | AV1 aggr hdr  |                                               |
// +-+-+-+-+-+-+-+-+                                               |
// |                                                               |
// |                   Bytes 2..N of AV1 payload                   |
// |                                                               |
// |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                               :    OPTIONAL RTP padding       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// | AV1 aggr hdr  |
//  0 1 2 3 4 5 6 7
// +-+-+-+-+-+-+-+-+
// |Z|Y| W |N|-|-|-|
// +-+-+-+-+-+-+-+-+

#define DEFAULT_MTU 1500
#define MAX_NALU_LEN 1400

class RtpPacket {
 public:
  typedef enum {
    H264 = 96,
    H264_FEC_SOURCE = 97,
    H264_FEC_REPAIR = 98,
    AV1 = 99,
    OPUS = 111,
    DATA = 127
  } PAYLOAD_TYPE;

  typedef enum { UNKNOWN = 0, NALU = 1, FU_A = 28, FU_B = 29 } NAL_UNIT_TYPE;

 public:
  RtpPacket();
  RtpPacket(const uint8_t *buffer, size_t size);
  RtpPacket(const RtpPacket &rtp_packet);
  RtpPacket(RtpPacket &&rtp_packet);
  RtpPacket &operator=(const RtpPacket &rtp_packet);
  RtpPacket &operator=(RtpPacket &&rtp_packet);

  ~RtpPacket();

 public:
  // Set Header
  void SetVerion(uint32_t version) { version_ = version; }
  void SetHasPadding(bool has_padding) { has_padding_ = has_padding; }
  void SetHasExtension(bool has_extension) { has_extension_ = has_extension; }
  void SetMarker(bool marker) { marker_ = marker; }
  void SetPayloadType(PAYLOAD_TYPE payload_type) {
    payload_type_ = payload_type;
  }
  void SetSequenceNumber(uint16_t sequence_number) {
    sequence_number_ = sequence_number;
  }
  void SetTimestamp(uint32_t timestamp) { timestamp_ = timestamp; }
  void SetSsrc(uint32_t ssrc) { ssrc_ = ssrc; }
  void SetCsrcs(std::vector<uint32_t> &csrcs) { csrcs_ = csrcs; }

  void SetExtensionProfile(uint16_t extension_profile) {
    extension_profile_ = extension_profile;
  }
  void SetExtensionData(uint8_t *extension_data, size_t extension_len) {
    extension_len_ = extension_len;
    extension_data_ = new uint8_t[extension_len_];
    memcpy(extension_data_, extension_data, extension_len_);
  }

 public:
  typedef struct {
    uint8_t forbidden_bit : 1;
    uint8_t nal_reference_idc : 2;
    uint8_t nal_unit_type : 5;
  } FU_INDICATOR;

  typedef struct {
    uint8_t start : 1;
    uint8_t end : 1;
    uint8_t remain_bit : 1;
    uint8_t nal_unit_type : 5;
  } FU_HEADER;

  void SetFuIndicator(FU_INDICATOR fu_indicator) {
    fu_indicator_.forbidden_bit = fu_indicator.forbidden_bit;
    fu_indicator_.nal_reference_idc = fu_indicator.nal_reference_idc;
    fu_indicator_.nal_unit_type = fu_indicator.nal_unit_type;
  }

  void SetFuHeader(FU_HEADER fu_header) {
    fu_header_.start = fu_header.start;
    fu_header_.end = fu_header.end;
    fu_header_.remain_bit = fu_header.remain_bit;
    fu_header_.nal_unit_type = fu_header.nal_unit_type;
  }

  void SetAv1AggrHeader(int z, int y, int w, int n) {
    if (z) av1_aggr_header_ |= (1 << 7);
    if (y) av1_aggr_header_ |= (1 << 6);
    if (w) av1_aggr_header_ |= w << 4;
    if (n) av1_aggr_header_ |= (1 << 3);
  }

  void SetFecSymbolId(uint8_t fec_symbol_id) { fec_symbol_id_ = fec_symbol_id; }

 public:
  const uint8_t *Encode(uint8_t *payload, size_t payload_size);
  const uint8_t *EncodeH264Nalu(uint8_t *payload, size_t payload_size);
  const uint8_t *EncodeH264Fua(uint8_t *payload, size_t payload_size);
  const uint8_t *EncodeH264FecSource(uint8_t *payload, size_t payload_size,
                                     unsigned int fec_symbol_id,
                                     unsigned int fec_source_symbol_num);
  const uint8_t *EncodeH264FecRepair(uint8_t *payload, size_t payload_size,
                                     unsigned int fec_symbol_id,
                                     unsigned int fec_source_symbol_num);
  const uint8_t *EncodeAv1(uint8_t *payload, size_t payload_size);

  size_t DecodeData(uint8_t *payload = nullptr);
  size_t DecodeH264Nalu(uint8_t *payload = nullptr);
  size_t DecodeH264Fua(uint8_t *payload = nullptr);
  size_t DecodeH264FecSource(uint8_t *payload = nullptr);
  size_t DecodeH264FecRepair(uint8_t *payload = nullptr);
  size_t DecodeAv1(uint8_t *payload = nullptr);
  size_t DecodeOpus(uint8_t *payload = nullptr);

 public:
  // Get Header
  uint32_t Verion() {
    ParseRtpData();
    return version_;
  }
  bool HasPadding() {
    ParseRtpData();
    return has_padding_;
  }
  bool HasExtension() {
    ParseRtpData();
    return has_extension_;
  }
  bool Marker() {
    ParseRtpData();
    return marker_;
  }
  PAYLOAD_TYPE PayloadType() {
    ParseRtpData();
    return PAYLOAD_TYPE(payload_type_);
  }
  uint16_t SequenceNumber() {
    ParseRtpData();
    return sequence_number_;
  }
  uint32_t Timestamp() {
    ParseRtpData();
    return timestamp_;
  }
  uint32_t Ssrc() {
    ParseRtpData();
    return ssrc_;
  }
  std::vector<uint32_t> Csrcs() {
    ParseRtpData();
    return csrcs_;
  };
  uint16_t ExtensionProfile() {
    ParseRtpData();
    return extension_profile_;
  }
  const uint8_t *ExtensionData() {
    ParseRtpData();
    return extension_data_;
  }

  uint8_t FecSymbolId() { return fec_symbol_id_; }

  uint8_t FecSourceSymbolNum() { return fec_source_symbol_num_; }

  void GetAv1AggrHeader(int &z, int &y, int &w, int &n) {
    z = av1_aggr_header_ >> 7;
    y = av1_aggr_header_ >> 6 & 0x01;
    w = av1_aggr_header_ >> 4 & 0x03;
    n = av1_aggr_header_ >> 3 & 0x01;
  }

  // Payload
  const uint8_t *Payload() {
    ParseRtpData();
    return payload_;
  };
  size_t PayloadSize() {
    ParseRtpData();
    return payload_size_;
  }

  // Entire RTP buffer
  const uint8_t *Buffer() const { return buffer_; }
  size_t Size() const { return size_; }

  // NAL
  NAL_UNIT_TYPE NalUnitType() {
    ParseRtpData();
    return nal_unit_type_;
  }
  bool FuAStart() {
    ParseRtpData();
    return fu_header_.start;
  }
  bool FuAEnd() {
    ParseRtpData();
    return fu_header_.end;
  }

  bool Av1FrameStart() {
    ParseRtpData();
    int z, y, w, n;
    GetAv1AggrHeader(z, y, w, n);
    return !z && !y;
  }

  bool Av1FrameEnd() {
    ParseRtpData();
    int z, y, w, n;
    GetAv1AggrHeader(z, y, w, n);
    return z && !y;
  }

 private:
  void TryToDecodeRtpPacket();
  void ParseRtpData();

 private:
  // Header
  uint32_t version_ = 0;
  bool has_padding_ = false;
  bool has_extension_ = false;
  uint32_t total_csrc_number_ = 0;
  bool marker_ = false;
  uint32_t payload_type_ = 0;
  uint16_t sequence_number_ = 0;
  uint32_t timestamp_ = 0;
  uint32_t ssrc_ = 0;
  std::vector<uint32_t> csrcs_;
  uint16_t profile_ = 0;
  uint16_t extension_profile_ = 0;
  uint16_t extension_len_ = 0;
  uint8_t *extension_data_ = nullptr;
  FU_INDICATOR fu_indicator_;
  FU_HEADER fu_header_;
  uint8_t fec_symbol_id_ = 0;
  uint8_t fec_source_symbol_num_ = 0;
  uint8_t av1_aggr_header_ = 0;

  // Payload
  uint8_t *payload_ = nullptr;
  size_t payload_size_ = 0;

  // Entire RTP buffer
  uint8_t *buffer_ = nullptr;
  size_t size_ = 0;

  // NAL
  NAL_UNIT_TYPE nal_unit_type_ = NAL_UNIT_TYPE::UNKNOWN;

  bool parsed_ = false;
};

#endif