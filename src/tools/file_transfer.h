/*
 * @Author: DI JUNKUN
 * @Date: 2025-12-18
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FILE_TRANSFER_H_
#define _FILE_TRANSFER_H_

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace crossdesk {

// Magic constants for file transfer protocol
constexpr uint32_t kFileChunkMagic = 0x4A4E544D;  // 'JNTM'
constexpr uint32_t kFileAckMagic = 0x4A4E5443;    // 'JNTC'

#pragma pack(push, 1)
struct FileChunkHeader {
  uint32_t magic;       // magic to identify file-transfer chunks
  uint32_t file_id;     // unique id per file transfer
  uint64_t offset;      // offset in file
  uint64_t total_size;  // total file size
  uint32_t chunk_size;  // payload size in this chunk
  uint16_t name_len;    // filename length (bytes), only set on first chunk
  uint8_t flags;        // bit0: is_first, bit1: is_last, others reserved
};

struct FileTransferAck {
  uint32_t magic;         // magic to identify file-transfer ack
  uint32_t file_id;       // must match FileChunkHeader.file_id
  uint64_t acked_offset;  // received offset
  uint64_t total_size;    // total file size
  uint32_t flags;         // bit0: completed, bit1: error
};
#pragma pack(pop)

class FileSender {
 public:
  using SendFunc = std::function<int(const char* data, size_t size)>;

 public:
  FileSender() = default;

  // generate a new file id
  static uint32_t NextFileId();

  // synchronously send a file using the provided send function.
  // `path`  : full path to the local file.
  // `label` : logical filename to send (usually path.filename()).
  // `send`  : callback that pushes one encoded chunk into the data channel.
  // `file_id` : file id to use (0 means auto-generate).
  // Return 0 on success, <0 on error.
  int SendFile(const std::filesystem::path& path, const std::string& label,
               const SendFunc& send, std::size_t chunk_size = 64 * 1024,
               uint32_t file_id = 0);

  // build a single encoded chunk buffer according to FileChunkHeader protocol.
  static std::vector<char> BuildChunk(uint32_t file_id, uint64_t offset,
                                      uint64_t total_size, const char* data,
                                      uint32_t data_size,
                                      const std::string* file_name,
                                      bool is_first, bool is_last);
};

class FileReceiver {
 public:
  struct FileContext {
    std::string file_name;
    uint64_t total_size = 0;
    uint64_t received = 0;
    std::ofstream ofs;
  };

  using OnFileComplete =
      std::function<void(const std::filesystem::path& saved_path)>;
  using OnSendAck = std::function<int(const FileTransferAck& ack)>;

 public:
  // save to default desktop directory.
  FileReceiver();

  // save to a specified directory.
  explicit FileReceiver(const std::filesystem::path& output_dir);

  // process one received data buffer (one chunk).
  // return true if parsed and processed successfully, false otherwise.
  bool OnData(const char* data, size_t size);

  void SetOnSendAck(OnSendAck cb) { on_send_ack_ = cb; }

  const std::filesystem::path& OutputDir() const { return output_dir_; }

 private:
  static std::filesystem::path GetDefaultDesktopPath();

  bool HandleChunk(const FileChunkHeader& header, const char* payload,
                   size_t payload_size, const std::string* file_name);

 private:
  std::filesystem::path output_dir_;
  std::unordered_map<uint32_t, FileContext> contexts_;
  OnSendAck on_send_ack_ = nullptr;
};

}  // namespace crossdesk

#endif