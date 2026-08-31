/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-01
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FILE_TRANSFER_FORMAT_H_
#define _FILE_TRANSFER_FORMAT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crossdesk {

inline constexpr uint32_t kFileChunkMagic = 0x4A4E544D;  // 'JNTM'
inline constexpr uint32_t kFileAckMagic = 0x4A4E5443;    // 'JNTC'

#pragma pack(push, 1)
struct FileChunkHeader {
  uint32_t magic;
  uint32_t file_id;
  uint64_t offset;
  uint64_t total_size;
  uint32_t chunk_size;
  uint16_t name_len;
  uint8_t flags;
};

struct FileTransferAck {
  uint32_t magic;
  uint32_t file_id;
  uint64_t acked_offset;
  uint64_t total_size;
  uint32_t flags;
};
#pragma pack(pop)

static_assert(sizeof(FileChunkHeader) == 31,
              "FileChunkHeader wire layout must remain stable");
static_assert(sizeof(FileTransferAck) == 28,
              "FileTransferAck wire layout must remain stable");

struct FileChunkView {
  FileChunkHeader header{};
  std::string file_name;
  const char* payload = nullptr;
  std::size_t payload_size = 0;
};

std::vector<char> EncodeFileChunk(uint32_t file_id, uint64_t offset,
                                  uint64_t total_size, const char* data,
                                  uint32_t data_size,
                                  const std::string* file_name, bool is_first,
                                  bool is_last);

bool DecodeFileChunk(const char* data, std::size_t size,
                     FileChunkView* output);

std::array<char, sizeof(FileTransferAck)> EncodeFileTransferAck(
    const FileTransferAck& ack);

bool DecodeFileTransferAck(const char* data, std::size_t size,
                           FileTransferAck* output);

}  // namespace crossdesk

#endif
