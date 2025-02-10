#ifndef _RTP_PACKET_H_
#define _RTP_PACKET_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "copy_on_write_buffer.h"
#include "log.h"
#include "rtp_defines.h"
#include "rtp_header.h"

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
//
// Z=1: first obu element is an obu fragment that is a continuation of an OBU
// fragment from the previous packet.
//
// Y=1: the last OBU element is an OBU fragment that will continue in the next
// packet.
//
// W=1: two bit field that describes the number of OBU elements in the packet.
//
// N=1: the packet is the first packet of a coded video sequence.
//
//  0 1 2 3 4 5 6 7
// +-+-+-+-+-+-+-+-+
// |Z|Y| W |N|-|-|-|
// +-+-+-+-+-+-+-+-+

#define DEFAULT_MTU 1500
#define MAX_NALU_LEN 1400

constexpr uint16_t kOneByteExtensionProfileId = 0xBEDE;
constexpr uint16_t kTwoByteExtensionProfileId = 0x1000;
constexpr size_t kFixedHeaderSize = 12;

class RtpPacket {
 public:
  RtpPacket();
  RtpPacket(size_t size);
  RtpPacket(const RtpPacket &rtp_packet);
  RtpPacket(RtpPacket &&rtp_packet);
  RtpPacket &operator=(const RtpPacket &rtp_packet);
  RtpPacket &operator=(RtpPacket &&rtp_packet);

  virtual ~RtpPacket();

 public:
  bool Build(const uint8_t *buffer, uint32_t size);

 private:
  bool Parse(const uint8_t *buffer, uint32_t size);

 public:
  // Set Header
  void SetVerion(uint8_t version) { version_ = version; }
  void SetHasPadding(bool has_padding) { has_padding_ = has_padding; }
  void SetHasExtension(bool has_extension) { has_extension_ = has_extension; }
  void SetMarker(bool marker) { marker_ = marker; }
  void SetPayloadType(rtp::PAYLOAD_TYPE payload_type) {
    payload_type_ = (uint8_t)payload_type;
  }
  void SetSequenceNumber(uint16_t sequence_number) {
    sequence_number_ = sequence_number;
  }
  void SetTimestamp(uint64_t timestamp) { timestamp_ = timestamp; }
  void SetSsrc(uint32_t ssrc) { ssrc_ = ssrc; }
  void SetCsrcs(std::vector<uint32_t> &csrcs) { csrcs_ = csrcs; }
  void SetSize(size_t size) { size_ = size; }

  void SetAbsoluteSendTimestamp(uint32_t abs_send_time) {
    // Absolute Send Time is a 24-bit field, we need to ensure it fits in 24
    // bits
    abs_send_time &= 0x00FFFFFF;

    // Set the extension profile to 0xBEDE (one-byte header)
    extension_profile_ = kOneByteExtensionProfileId;
    extension_len_ = 5;  // 2 bytes for profile, 2 bytes for length, 3 bytes for
                         // abs_send_time

    Extension extension;
    extension.id = 0;
    extension.len = 2;
    extension.data.push_back(extension.id << 4 | extension.len);
    extension.data.push_back((abs_send_time >> 16) & 0xFF);
    extension.data.push_back((abs_send_time >> 8) & 0xFF);
    extension.data.push_back(abs_send_time & 0xFF);
  }

 public:
  // Get Header
  uint32_t Version() const { return version_; }
  bool HasPadding() const { return has_padding_; }
  bool HasExtension() const { return has_extension_; }
  bool Marker() const { return marker_; }
  rtp::PAYLOAD_TYPE PayloadType() const {
    return rtp::PAYLOAD_TYPE(payload_type_);
  }
  uint16_t SequenceNumber() const { return sequence_number_; }
  uint64_t Timestamp() const { return timestamp_; }
  uint32_t Ssrc() const { return ssrc_; }
  std::vector<uint32_t> Csrcs() const { return csrcs_; };
  uint16_t ExtensionProfile() const { return extension_profile_; }

  uint32_t GetAbsoluteSendTimestamp(uint32_t *abs_send_time) const {
    if (!extensions_.empty()) {
      for (auto &ext : extensions_) {
        if (ext.id == 1) {
          *abs_send_time = (ext.data[0] << 16) | (ext.data[1] << 8) |
                           ext.data[2];  // 24-bit value
          return *abs_send_time;
        }
      }
    }
    return 0;
  }

  // Payload
  const uint8_t *Payload() { return Buffer().data() + payload_offset_; };
  size_t PayloadSize() { return payload_size_; }

  // Entire RTP buffer
  CopyOnWriteBuffer Buffer() const { return buffer_; }
  size_t Size() const { return size_; }

  // Header
  const uint8_t *Header() { return Buffer().data(); };
  size_t HeaderSize() { return payload_offset_; }

  // For webrtc module use
  size_t headers_size() const { return payload_offset_; }
  size_t payload_size() const { return payload_size_; }
  bool has_padding() const { return buffer_[0] & 0x20; }
  size_t padding_size() const { return padding_size_; }
  size_t size() const { return size_; }
  void add_offset_to_payload(size_t offset) {
    payload_offset_ += offset;
    payload_size_ -= offset;
  }

 private:
  // Common header
  uint8_t version_ = 0;
  bool has_padding_ = false;
  bool has_extension_ = false;
  uint8_t csrc_count_ = 0;
  bool marker_ = false;
  uint8_t payload_type_ = 0;
  uint16_t sequence_number_ = 1;
  uint64_t timestamp_ = 0;
  uint32_t ssrc_ = 0;
  std::vector<uint32_t> csrcs_;

  // Extension header
  uint16_t extension_profile_ = 0;
  uint16_t extension_len_ = 0;
  struct Extension {
    uint8_t id;
    uint8_t len;
    std::vector<uint8_t> data;
  };
  std::vector<Extension> extensions_;

  // Payload
  size_t payload_offset_ = 0;
  size_t payload_size_ = 0;

  // Padding
  size_t padding_size_ = 0;

  // Entire rtp buffer
  CopyOnWriteBuffer buffer_;
  size_t size_ = 0;
};

#endif