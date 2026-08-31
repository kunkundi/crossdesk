#include <file_transfer_format.h>

#include <cstring>
#include <limits>

namespace crossdesk {

std::vector<char> EncodeFileChunk(uint32_t file_id, uint64_t offset,
                                  uint64_t total_size, const char* data,
                                  uint32_t data_size,
                                  const std::string* file_name, bool is_first,
                                  bool is_last) {
  const std::size_t name_size = file_name && is_first ? file_name->size() : 0;
  if (name_size > std::numeric_limits<uint16_t>::max() ||
      offset > total_size || data_size > total_size - offset ||
      (data_size > 0 && data == nullptr)) {
    return {};
  }

  FileChunkHeader header{};
  header.magic = kFileChunkMagic;
  header.file_id = file_id;
  header.offset = offset;
  header.total_size = total_size;
  header.chunk_size = data_size;
  header.name_len = static_cast<uint16_t>(name_size);
  header.flags = (is_first ? 0x01 : 0) | (is_last ? 0x02 : 0);

  std::vector<char> output(sizeof(header) + name_size + data_size);
  std::memcpy(output.data(), &header, sizeof(header));
  std::size_t cursor = sizeof(header);
  if (name_size > 0) {
    std::memcpy(output.data() + cursor, file_name->data(), name_size);
    cursor += name_size;
  }
  if (data_size > 0) {
    std::memcpy(output.data() + cursor, data, data_size);
  }
  return output;
}

bool DecodeFileChunk(const char* data, std::size_t size,
                     FileChunkView* output) {
  if (!data || !output || size < sizeof(FileChunkHeader)) return false;

  FileChunkHeader header{};
  std::memcpy(&header, data, sizeof(header));
  if (header.magic != kFileChunkMagic) return false;

  const std::size_t name_size = header.name_len;
  const std::size_t payload_size = header.chunk_size;
  if (name_size > size - sizeof(header)) return false;
  const std::size_t payload_offset = sizeof(header) + name_size;
  if (payload_size > size - payload_offset || header.offset > header.total_size ||
      payload_size > header.total_size - header.offset) {
    return false;
  }

  output->header = header;
  output->file_name.assign(data + sizeof(header), name_size);
  output->payload = data + payload_offset;
  output->payload_size = payload_size;
  return true;
}

std::array<char, sizeof(FileTransferAck)> EncodeFileTransferAck(
    const FileTransferAck& ack) {
  std::array<char, sizeof(FileTransferAck)> output{};
  std::memcpy(output.data(), &ack, sizeof(ack));
  return output;
}

bool DecodeFileTransferAck(const char* data, std::size_t size,
                           FileTransferAck* output) {
  if (!data || !output || size != sizeof(FileTransferAck)) return false;
  std::memcpy(output, data, sizeof(*output));
  return output->magic == kFileAckMagic &&
         output->acked_offset <= output->total_size;
}

}  // namespace crossdesk
