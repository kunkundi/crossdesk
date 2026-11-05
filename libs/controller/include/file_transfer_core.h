/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FILE_TRANSFER_CORE_H_
#define _FILE_TRANSFER_CORE_H_

#include <file_transfer_format.h>
#include <stream_names.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace crossdesk::file_transfer {

// UI, RTC peers and platform permissions stay in the adapters. Only mobile
// adapters transfer ownership of disposable picker copies to this engine.
struct Options {
  bool remove_source = false;
  bool receive_subdirectory = false;
  size_t max_incoming = 0;
};

enum class Status { Idle, Sending, Completed, Failed, Cancelled };
struct SendState {
  uint32_t id = 0;
  uint64_t total = 0, acknowledged = 0;
  Status status = Status::Idle;
};

enum class ReceiveStatus { Receiving, Completed, Failed, Cancelled };
struct ReceiveState {
  uint32_t id = 0;
  std::string name;
  uint64_t total = 0, received = 0;
  ReceiveStatus status = ReceiveStatus::Receiving;
};

class Session {
 public:
  using Clock = std::chrono::steady_clock;
  using Send = std::function<int(const char*, const char*, size_t)>;
  // Progress callbacks must not reenter the session. Transport callbacks run
  // outside its mutex, allowing immediate/reentrant ACKs and concurrent peers.
  using Progress = std::function<void(const std::string&, double, bool,
                                      const std::string&)>;
  // Exact accepted bytes and terminal state, keyed by this peer's wire ID.
  using ReceiveProgress = std::function<void(const ReceiveState&)>;
  Session(Send send, Progress progress, Options options = {},
          ReceiveProgress receive_progress = {})
      : send_(std::move(send)), progress_(std::move(progress)),
        receive_progress_(std::move(receive_progress)), options_(options) {}
  ~Session() { Clear(); }

  static uint32_t NextFileId() {
    static std::atomic<uint32_t> next{1};
    uint32_t id;
    do { id = next.fetch_add(1); } while (id == 0);
    return id;
  }
  void SetDirectory(std::filesystem::path directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    directory_ = std::move(directory);
  }
  // A browser destination belongs to one wire ID, never to the whole peer.
  // Keep reservations until consumed: other queued files may take hours.
  bool ReserveDirectory(uint32_t id, const std::filesystem::path& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || !id || incoming_.count(id) || cancelled_incoming_.count(id) ||
        destinations_.size() >= 8192)
      return false;
    return destinations_.emplace(id, directory).second;
  }
  bool Start(const std::filesystem::path& path, const std::string& name,
             Clock::time_point now = Clock::now(), uint32_t id = 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || outgoing_ || !send_ || name.empty() || name.size() > UINT16_MAX) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > INT64_MAX) return false;
    Outgoing file;
    file.input.open(path, std::ios::binary);
    if (!file.input) return false;
    file.id = id ? id : NextFileId();
    file.name = name; file.path = path; file.total = size; file.updated = now;
    state_ = {file.id, size, 0, Status::Sending};
    outgoing_ = std::move(file);
    Notify(name, 0, true, "");
    return true;
  }

  // At most one chunk per pump; bound queued but unacknowledged bytes to 1 MiB.
  bool Pump(Clock::time_point now = Clock::now()) {
    std::vector<FileTransferAck> expired;
    std::vector<char> chunk;
    uint32_t id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) return false;
      for (auto it = cancelled_incoming_.begin(); it != cancelled_incoming_.end();) {
        if (now - it->second >= timeout_) it = cancelled_incoming_.erase(it);
        else ++it;
      }
      for (auto it = incoming_.begin(); it != incoming_.end();) {
        if (now - it->second.updated < timeout_) { ++it; continue; }
        expired.push_back({kFileAckMagic, it->first, it->second.received, it->second.total, 2});
        NotifyReceive(it->first, it->second, ReceiveStatus::Failed);
        Notify(it->second.name, -1, false, "");
        Remove(it->second); it = incoming_.erase(it);
      }
      if (outgoing_) {
        auto& file = *outgoing_;
        if (now - file.updated >= timeout_) FinishSend(false);
        else if (!file.last && file.sent - file.acked < 1024 * 1024) {
          std::array<char, kFileChunkSize> payload{};
          const auto size = static_cast<uint32_t>((std::min<uint64_t>)(payload.size(), file.total - file.sent));
          if (size) file.input.read(payload.data(), size);
          if (size && file.input.gcount() != size) FinishSend(false);
          else {
            const bool first = file.sent == 0;
            const bool last = file.sent + size == file.total;
            chunk = EncodeFileChunk(file.id, file.sent, file.total, payload.data(), size,
                                    first ? &file.name : nullptr, first, last);
            if (chunk.empty()) FinishSend(false);
            else {
              id = file.id;
              // Reserve before calling transport: an ACK can arrive before Send returns.
              file.sent += size; file.last = last;
            }
          }
        }
      }
    }
    for (const auto& ack : expired) SendAck(ack);
    if (!chunk.empty() && send_(kFileStream, chunk.data(), chunk.size()) != 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (outgoing_ && outgoing_->id == id) FinishSend(false);
    }
    return !chunk.empty();
  }

  void Ack(const char* data, size_t size, Clock::time_point now = Clock::now()) {
    FileTransferAck ack{};
    if (!DecodeFileTransferAck(data, size, &ack)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!outgoing_ || ack.file_id != outgoing_->id) return;
    auto& file = *outgoing_;
    if (ack.total_size != file.total || ack.acked_offset > file.sent || (ack.flags & ~3u)) return;
    if (ack.flags & 2) { FinishSend(false); return; }
    if (ack.flags & 1) {
      if (file.last && ack.acked_offset == file.total) {
        state_.acknowledged = file.total;
        FinishSend(true);
      }
      return;
    }
    if (ack.acked_offset <= file.acked) return;
    file.acked = ack.acked_offset; file.updated = now;
    state_.acknowledged = file.acked;
    Report(file.name, file.acked, file.total, true, file.percent);
  }

  bool Receive(const char* data, size_t size, Clock::time_point now = Clock::now()) {
    FileChunkView chunk;
    if (!DecodeFileChunk(data, size, &chunk)) return false;
    FileTransferAck ack;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) return false;
      ack = ReceiveLocked(chunk, size, now);
    }
    SendAck(ack);
    return (ack.flags & 2) == 0;
  }
  bool Sending() const { return Snapshot().status == Status::Sending; }
  SendState Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }
  // Stop only the outgoing file. Keep the peer usable for subsequent sends.
  // The adapter calls this between pumps; late/reordered chunks are discarded
  // by the receiver's cancellation tombstone.
  bool CancelSend() {
    std::vector<char> packet;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!outgoing_) return false;
      packet = EncodeFileCancel(outgoing_->id, outgoing_->total);
      FinishSend(false, true);
    }
    if (send_) send_(kFileStream, packet.data(), packet.size());
    return true;
  }
  // A cancelled connection cannot be restarted by a late data callback.
  void Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    if (outgoing_) FinishSend(false);
    for (const auto& [id, file] : incoming_)
      NotifyReceive(id, file, ReceiveStatus::Failed);
    Clear();
  }

 private:
  struct Outgoing {
    uint32_t id = 0;
    std::string name;
    std::filesystem::path path;
    std::ifstream input;
    uint64_t total = 0, sent = 0, acked = 0;
    bool last = false;
    int percent = -1;
    Clock::time_point updated;
  };
  struct Incoming {
    std::string name;
    std::filesystem::path path, partial, folder, directory;
    std::ofstream output;
    uint64_t total = 0, received = 0;
    bool first = false, last = false;
    std::map<uint64_t, uint64_t> ranges;
    int percent = -1;
    Clock::time_point updated;
  };
  void NotifyReceive(uint32_t id, const Incoming& file, ReceiveStatus status) {
    if (receive_progress_)
      receive_progress_({id, file.name, file.total, file.received, status});
  }
  FileTransferAck ReceiveLocked(const FileChunkView& chunk, size_t size, Clock::time_point now) {
    const auto& h = chunk.header;
    auto it = incoming_.find(h.file_id);
    if (h.flags == kFileCancelFlag) {
      const FileTransferAck ack{kFileAckMagic, h.file_id, 0, h.total_size, 2};
      if (size != sizeof(FileChunkHeader) || h.offset || h.name_len ||
          h.chunk_size || h.total_size > INT64_MAX ||
          (it != incoming_.end() && h.total_size != it->second.total))
        return ack;
      cancelled_incoming_[h.file_id] = now;
      destinations_.erase(h.file_id);
      if (it != incoming_.end()) {
        NotifyReceive(h.file_id, it->second, ReceiveStatus::Cancelled);
        Notify(it->second.name, -1, false, "");
        Remove(it->second);
        incoming_.erase(it);
      }
      return ack;
    }
    // KCP receive callbacks can be reordered. Do not recreate a cancelled
    // staging file or a second notification when an in-flight chunk arrives.
    if (cancelled_incoming_.count(h.file_id))
      return {kFileAckMagic, h.file_id, 0, h.total_size, 2};
    auto reject = [&]() {
      destinations_.erase(h.file_id);
      if (it != incoming_.end()) {
        NotifyReceive(h.file_id, it->second, ReceiveStatus::Failed);
        Notify(it->second.name, -1, false, ""); Remove(it->second); incoming_.erase(it);
      } else {
        if (receive_progress_)
          receive_progress_({h.file_id, SafeName(chunk.file_name), h.total_size,
                             0, ReceiveStatus::Failed});
        Notify(SafeName(chunk.file_name), -1, false, "");
      }
      return FileTransferAck{kFileAckMagic, h.file_id, h.offset, h.total_size, 2};
    };
    const bool first = h.flags & 1, last = h.flags & 2;
    if ((h.flags & ~3u) || h.total_size > INT64_MAX || (first && h.offset != 0) ||
        chunk.payload_size > kFileChunkSize || size != sizeof(FileChunkHeader) + h.name_len + chunk.payload_size ||
        (!first && h.name_len) || (!chunk.payload_size && !last) ||
        last != (h.offset + chunk.payload_size == h.total_size) ||
        (it == incoming_.end() && options_.max_incoming && incoming_.size() >= options_.max_incoming))
      return reject();
    if (it == incoming_.end()) {
      Incoming file;
      file.name = SafeName(chunk.file_name); file.total = h.total_size;
      file.directory = directory_.empty() ? std::filesystem::path(".") : directory_;
      if (const auto destination = destinations_.find(h.file_id); destination != destinations_.end()) {
        file.directory = destination->second;
        destinations_.erase(destination);
      }
      std::error_code ec;
      std::filesystem::create_directories(file.directory, ec);
      if (ec || (directory_.empty() && options_.receive_subdirectory)) return reject();
      // Reserve a unique staging directory even when two peers use the same ID.
      static std::atomic<uint64_t> serial{0};
      for (int attempt = 0; attempt < 100; ++attempt) {
        const auto token = std::to_string(h.file_id) + "-" +
            std::to_string(Clock::now().time_since_epoch().count()) + "-" + std::to_string(++serial);
        file.folder = file.directory / (options_.receive_subdirectory ? token : ".crossdesk-" + token);
        if (std::filesystem::create_directory(file.folder, ec)) break;
        if (ec || attempt == 99) return reject();
      }
      file.path = file.folder / NamePath(file.name);
      file.partial = file.folder / ".partial";
      file.output.open(file.partial, std::ios::binary | std::ios::trunc);
      if (!file.output) { Remove(file); return reject(); }
      it = incoming_.emplace(h.file_id, std::move(file)).first;
    }
    auto& file = it->second;
    if (h.total_size != file.total || (first && file.first) ||
        !AddRange(file, h.offset, h.offset + chunk.payload_size)) return reject();
    if (first) {
      file.first = true; file.name = SafeName(chunk.file_name);
      file.path = file.folder / NamePath(file.name);
    }
    file.last |= last;
    // KCP's RTP and timer callbacks can race: ACK only contiguous coverage.
    file.received = !file.ranges.empty() && file.ranges.begin()->first == 0 ? file.ranges.begin()->second : 0;
    const bool complete = file.first && file.last && file.received == file.total;
    file.output.seekp(static_cast<std::streamoff>(h.offset));
    file.output.write(chunk.payload, chunk.payload_size);
    if (complete) { file.output.flush(); file.output.close(); }
    if (file.output.fail() || (complete && !Publish(file))) return reject();
    file.updated = now;
    const FileTransferAck ack{kFileAckMagic, h.file_id, file.received, file.total, complete ? 1u : 0u};
    NotifyReceive(h.file_id, file, complete ? ReceiveStatus::Completed : ReceiveStatus::Receiving);
    if (complete) {
      const auto path = file.path.u8string();
      Notify(file.name, 1, false, std::string(path.begin(), path.end()));
      incoming_.erase(it);
    } else Report(file.name, file.received, file.total, false, file.percent);
    return ack;
  }
  bool Publish(Incoming& file) {
    std::error_code ec;
    if (options_.receive_subdirectory) {
      std::filesystem::rename(file.partial, file.path, ec);
      return !ec;
    }
    // Preserve the desktop's flat destination and timestamp suffix on conflict.
    // Hard-link publication is atomic and never overwrites another peer's file.
    const auto original = NamePath(file.name);
    const auto stamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
      auto name = original;
      if (attempt) {
        name = original.stem(); name += "_" + stamp;
        if (attempt > 1) name += "_" + std::to_string(attempt);
        name += original.extension();
      }
      file.path = file.directory / name;
      ec.clear();
      std::filesystem::create_hard_link(file.partial, file.path, ec);
      if (!ec) { Remove(file); return true; }
      if (ec == std::errc::file_exists) continue;
      // Some public folder providers disallow links. Exclusively create the
      // destination so a failed copy can remove only the file we just created.
#if defined(_WIN32)
      std::FILE* output = _wfopen(file.path.c_str(), L"wbx");
#else
      std::FILE* output = std::fopen(file.path.c_str(), "wbx");
#endif
      if (!output) {
        if (errno == EEXIST) continue;
        return false;
      }
      std::ifstream input(file.partial, std::ios::binary);
      bool ok = input.is_open();
      std::array<char, kFileChunkSize> buffer{};
      while (ok && input) {
        input.read(buffer.data(), buffer.size());
        const auto count = static_cast<size_t>(input.gcount());
        if (count && std::fwrite(buffer.data(), 1, count, output) != count) ok = false;
      }
      ok = ok && input.eof() && !input.bad();
      input.close();
      if (std::fclose(output) != 0) ok = false;
      if (!ok) { std::filesystem::remove(file.path, ec); return false; }
      Remove(file); return true;
    }
    return false;
  }
  static bool AddRange(Incoming& file, uint64_t start, uint64_t end) {
    if (start == end) return true;
    auto next = file.ranges.lower_bound(start);
    if (next != file.ranges.end() && next->first < end) return false;
    if (next != file.ranges.begin()) {
      auto previous = std::prev(next);
      if (previous->second > start) return false;
      if (previous->second == start) { start = previous->first; file.ranges.erase(previous); }
    }
    if (next != file.ranges.end() && next->first == end) { end = next->second; file.ranges.erase(next); }
    file.ranges.emplace(start, end);
    return file.ranges.size() <= 1024;
  }
  static std::filesystem::path NamePath(const std::string& name) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(name.begin(), name.end()));
#else
    return std::filesystem::u8path(name);
#endif
  }
  static std::string SafeName(std::string name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    for (auto& ch : name)
      if (static_cast<unsigned char>(ch) < 32 || std::string(":*?\"<>|").find(ch) != std::string::npos) ch = '_';
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty() || name == ".partial" || name.size() > 240) return "received_file";
    return name;
  }
  static void Remove(Incoming& file) {
    if (file.output.is_open()) file.output.close();
    std::error_code ec;
    std::filesystem::remove(file.partial, ec);
    std::filesystem::remove(file.folder, ec);
  }
  void SendAck(const FileTransferAck& ack) {
    if (!send_) return;
    const auto packet = EncodeFileTransferAck(ack);
    send_(kFileFeedbackStream, packet.data(), packet.size());
  }
  void Notify(const std::string& name, double value, bool sending, const std::string& path) {
    if (progress_) progress_(name, value, sending, path);
  }
  void Report(const std::string& name, uint64_t offset, uint64_t total, bool sending, int& previous) {
    const double progress = total ? static_cast<double>(offset) / total : 0;
    const int percent = static_cast<int>(progress * 100);
    if (percent != previous) { previous = percent; Notify(name, (std::min)(progress, .99), sending, ""); }
  }
  void RemoveSource() {
    outgoing_->input.close();
    if (options_.remove_source) {
      std::error_code ec;
      std::filesystem::remove(outgoing_->path, ec);
    }
    outgoing_.reset();
  }
  void FinishSend(bool success, bool cancelled = false) {
    state_.status = success ? Status::Completed
        : cancelled ? Status::Cancelled : Status::Failed;
    Notify(outgoing_->name, success ? 1 : -1, true, "");
    RemoveSource();
  }
  void Clear() {
    if (outgoing_) RemoveSource();
    for (auto& entry : incoming_) Remove(entry.second);
    incoming_.clear();
    cancelled_incoming_.clear();
    destinations_.clear();
  }
  const std::chrono::seconds timeout_{30};
  mutable std::mutex mutex_;
  Send send_;
  Progress progress_;
  ReceiveProgress receive_progress_;
  Options options_;
  bool closed_ = false;
  SendState state_;
  std::filesystem::path directory_;
  std::optional<Outgoing> outgoing_;
  std::map<uint32_t, Incoming> incoming_;
  std::map<uint32_t, std::filesystem::path> destinations_;
  std::map<uint32_t, Clock::time_point> cancelled_incoming_;
};

}  // namespace crossdesk::file_transfer

#endif
