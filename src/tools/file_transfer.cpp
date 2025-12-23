#include "file_transfer.h"

#include <atomic>
#include <chrono>

#include "rd_log.h"

namespace crossdesk {

namespace {
std::atomic<uint32_t> g_next_file_id{1};
constexpr uint32_t kFileChunkMagic = 0x4A4E544D;  // 'JNTM'
}  // namespace

uint32_t FileSender::NextFileId() { return g_next_file_id.fetch_add(1); }

int FileSender::SendFile(const std::filesystem::path& path,
                         const std::string& label, const SendFunc& send,
                         std::size_t chunk_size) {
  if (!send) {
    LOG_ERROR("FileSender::SendFile: send function is empty");
    return -1;
  }

  std::error_code ec;
  if (!std::filesystem::exists(path, ec) ||
      !std::filesystem::is_regular_file(path, ec)) {
    LOG_ERROR("FileSender::SendFile: file [{}] not found or not regular",
              path.string().c_str());
    return -1;
  }

  uint64_t total_size = std::filesystem::file_size(path, ec);
  if (ec) {
    LOG_ERROR("FileSender::SendFile: failed to get size of [{}]: {}",
              path.string().c_str(), ec.message().c_str());
    return -1;
  }

  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    LOG_ERROR("FileSender::SendFile: failed to open [{}]",
              path.string().c_str());
    return -1;
  }
  LOG_INFO("FileSender send file {}, total size {}", path.string().c_str(),
           total_size);

  const uint32_t file_id = NextFileId();
  uint64_t offset = 0;
  bool is_first = true;
  std::string file_name = label.empty() ? path.filename().string() : label;

  std::vector<char> buffer;
  buffer.resize(chunk_size);

  while (ifs && offset < total_size) {
    uint64_t remaining = total_size - offset;
    uint32_t to_read =
        static_cast<uint32_t>(std::min<uint64_t>(remaining, chunk_size));

    ifs.read(buffer.data(), static_cast<std::streamsize>(to_read));
    std::streamsize bytes_read = ifs.gcount();
    if (bytes_read <= 0) {
      break;
    }

    bool is_last = (offset + static_cast<uint64_t>(bytes_read) >= total_size);
    const std::string* name_ptr = is_first ? &file_name : nullptr;

    std::vector<char> chunk = BuildChunk(
        file_id, offset, total_size, buffer.data(),
        static_cast<uint32_t>(bytes_read), name_ptr, is_first, is_last);

    int ret = send(chunk.data(), chunk.size());
    if (ret != 0) {
      LOG_ERROR("FileSender::SendFile: send failed for [{}], ret={}",
                path.string().c_str(), ret);
      return ret;
    }

    offset += static_cast<uint64_t>(bytes_read);
    is_first = false;
  }

  return 0;
}

std::vector<char> FileSender::BuildChunk(uint32_t file_id, uint64_t offset,
                                         uint64_t total_size, const char* data,
                                         uint32_t data_size,
                                         const std::string* file_name,
                                         bool is_first, bool is_last) {
  FileChunkHeader header{};
  header.magic = kFileChunkMagic;
  header.file_id = file_id;
  header.offset = offset;
  header.total_size = total_size;
  header.chunk_size = data_size;
  header.name_len =
      (file_name && is_first) ? static_cast<uint16_t>(file_name->size()) : 0;
  header.flags = 0;
  if (is_first) header.flags |= 0x01;
  if (is_last) header.flags |= 0x02;

  std::size_t total_size_bytes =
      sizeof(FileChunkHeader) + header.name_len + header.chunk_size;

  std::vector<char> buffer;
  buffer.resize(total_size_bytes);

  std::size_t offset_bytes = 0;
  memcpy(buffer.data() + offset_bytes, &header, sizeof(FileChunkHeader));
  offset_bytes += sizeof(FileChunkHeader);

  if (header.name_len > 0 && file_name) {
    memcpy(buffer.data() + offset_bytes, file_name->data(), header.name_len);
    offset_bytes += header.name_len;
  }

  if (header.chunk_size > 0 && data) {
    memcpy(buffer.data() + offset_bytes, data, header.chunk_size);
  }

  return buffer;
}

// ---------- FileReceiver ----------

FileReceiver::FileReceiver() : output_dir_(GetDefaultDesktopPath()) {}

FileReceiver::FileReceiver(const std::filesystem::path& output_dir)
    : output_dir_(output_dir) {
  std::error_code ec;
  if (!output_dir_.empty()) {
    std::filesystem::create_directories(output_dir_, ec);
    if (ec) {
      LOG_ERROR("FileReceiver: failed to create output dir [{}]: {}",
                output_dir_.string().c_str(), ec.message().c_str());
    }
  }
}

std::filesystem::path FileReceiver::GetDefaultDesktopPath() {
#ifdef _WIN32
  const char* home_env = std::getenv("USERPROFILE");
#else
  const char* home_env = std::getenv("HOME");
#endif
  if (!home_env) {
    return std::filesystem::path{};
  }

  std::filesystem::path desktop_path =
      std::filesystem::path(home_env) / "Desktop";

  std::error_code ec;
  std::filesystem::create_directories(desktop_path, ec);
  if (ec) {
    LOG_ERROR("FileReceiver: failed to create desktop directory [{}]: {}",
              desktop_path.string().c_str(), ec.message().c_str());
  }

  return desktop_path;
}

bool FileReceiver::OnData(const char* data, size_t size) {
  if (!data || size < sizeof(FileChunkHeader)) {
    LOG_ERROR("FileReceiver::OnData: invalid buffer");
    return false;
  }

  FileChunkHeader header{};
  memcpy(&header, data, sizeof(FileChunkHeader));

  if (header.magic != kFileChunkMagic) {
    return false;
  }

  std::size_t header_and_name =
      sizeof(FileChunkHeader) + static_cast<std::size_t>(header.name_len);
  if (size < header_and_name ||
      size < header_and_name + static_cast<std::size_t>(header.chunk_size)) {
    LOG_ERROR("FileReceiver::OnData: buffer too small for header + payload");
    return false;
  }

  const char* name_ptr = data + sizeof(FileChunkHeader);
  std::string file_name;
  const std::string* file_name_ptr = nullptr;
  if (header.name_len > 0) {
    file_name.assign(name_ptr,
                     name_ptr + static_cast<std::size_t>(header.name_len));
    file_name_ptr = &file_name;
  }

  const char* payload = data + header_and_name;
  std::size_t payload_size =
      static_cast<std::size_t>(header.chunk_size);  // may be 0

  return HandleChunk(header, payload, payload_size, file_name_ptr);
}

bool FileReceiver::HandleChunk(const FileChunkHeader& header,
                               const char* payload, size_t payload_size,
                               const std::string* file_name) {
  auto it = contexts_.find(header.file_id);
  if (it == contexts_.end()) {
    // new file context must start with first chunk.
    if ((header.flags & 0x01) == 0) {
      LOG_ERROR("FileReceiver: received non-first chunk for unknown file_id={}",
                header.file_id);
      return false;
    }

    FileContext ctx;
    ctx.total_size = header.total_size;

    std::string filename;
    if (file_name && !file_name->empty()) {
      filename = *file_name;
    } else {
      filename = "received_" + std::to_string(header.file_id);
    }

    ctx.file_name = filename;

    std::filesystem::path save_path = output_dir_.empty()
                                          ? std::filesystem::path(filename)
                                          : output_dir_ / filename;

    // if file exists, append timestamp.
    std::error_code ec;
    if (std::filesystem::exists(save_path, ec)) {
      auto now = std::chrono::system_clock::now();
      auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
      save_path = save_path.parent_path() /
                  (save_path.stem().string() + "_" + std::to_string(ts) +
                   save_path.extension().string());
    }

    ctx.ofs.open(save_path, std::ios::binary | std::ios::trunc);
    if (!ctx.ofs.is_open()) {
      LOG_ERROR("FileReceiver: failed to open [{}] for writing",
                save_path.string().c_str());
      return false;
    }

    contexts_.emplace(header.file_id, std::move(ctx));
    it = contexts_.find(header.file_id);
  }

  FileContext& ctx = it->second;

  if (payload_size > 0 && payload) {
    ctx.ofs.seekp(static_cast<std::streamoff>(header.offset), std::ios::beg);
    ctx.ofs.write(payload, static_cast<std::streamsize>(payload_size));
    if (!ctx.ofs.good()) {
      LOG_ERROR("FileReceiver: write failed for file_id={}", header.file_id);
      return false;
    }
    ctx.received += static_cast<uint64_t>(payload_size);
  }

  bool is_last = (header.flags & 0x02) != 0;
  if (is_last || ctx.received >= ctx.total_size) {
    ctx.ofs.close();

    std::filesystem::path saved_path =
        output_dir_.empty() ? std::filesystem::path(ctx.file_name)
                            : output_dir_ / ctx.file_name;

    LOG_INFO("FileReceiver: file received complete, file_id={}, size={}",
             header.file_id, ctx.received);

    if (on_file_complete_) {
      on_file_complete_(saved_path);
    }

    contexts_.erase(header.file_id);
  }

  return true;
}

}  // namespace crossdesk
