/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FILE_TRANSFER_SESSION_H_
#define _FILE_TRANSFER_SESSION_H_

#include <file_transfer_format.h>
#include <stream_names.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

namespace crossdesk::android_controller {

// File I/O is bounded to one chunk per pump. Feedback is delivered by MiniRTC
// callback threads; no callback waits for the RTC executor or for a remote ACK.
class FileTransferSession {
 public:
  using Clock = std::chrono::steady_clock;
  using Send = std::function<int(const char*, const char*, size_t)>;
  using Progress = std::function<void(const std::string&, double, bool,
                                      const std::string&)>;
  FileTransferSession(Send send, Progress progress)
      : send_(std::move(send)), progress_(std::move(progress)) {}
  ~FileTransferSession() {
    if (outgoing_) {
      outgoing_->input.close();
      std::error_code ec;
      std::filesystem::remove(outgoing_->path, ec);
    }
    for (auto& entry : incoming_) Remove(entry.second);
  }

  void SetDirectory(std::filesystem::path directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    directory_ = std::move(directory);
  }

  bool Start(const std::filesystem::path& path, const std::string& name,
             Clock::time_point now = Clock::now()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outgoing_ || name.empty() || name.size() > UINT16_MAX) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    Outgoing file;
    file.input.open(path, std::ios::binary);
    if (!file.input) return false;
    file.id = ++next_id_;
    file.name = name;
    file.path = path;
    file.total = size;
    file.updated = now;
    outgoing_ = std::move(file);
    progress_(name, 0, true, "");
    return true;
  }

  void Pump(Clock::time_point now = Clock::now()) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = incoming_.begin(); it != incoming_.end();) {
      if (now - it->second.updated < timeout_) { ++it; continue; }
      Reject(it->first, it->second.total, it->second.received);
      progress_(it->second.name, -1, false, "");
      Remove(it->second);
      it = incoming_.erase(it);
    }
    if (!outgoing_) return;
    auto& file = *outgoing_;
    if (now - file.updated >= timeout_) { FinishSend(false); return; }
    if (file.last || file.sent - file.acked >= 1024 * 1024) return;
    std::array<char, kFileChunkSize> payload{};
    const auto size = static_cast<uint32_t>(std::min<uint64_t>(payload.size(), file.total - file.sent));
    if (size) {
      file.input.read(payload.data(), size);
      if (file.input.gcount() != size) { FinishSend(false); return; }
    }
    const bool first = file.sent == 0;
    const bool last = file.sent + size == file.total;
    const auto chunk = EncodeFileChunk(file.id, file.sent, file.total,
        payload.data(), size, first ? &file.name : nullptr, first, last);
    if (chunk.empty() || send_(kFileStream, chunk.data(), chunk.size()) != 0) {
      FinishSend(false); return;
    }
    file.sent += size;
    file.last = last;
  }

  void Ack(const char* data, size_t size, Clock::time_point now = Clock::now()) {
    FileTransferAck ack{};
    if (!DecodeFileTransferAck(data, size, &ack)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!outgoing_ || ack.file_id != outgoing_->id) return;
    auto& file = *outgoing_;
    if (ack.total_size != file.total || ack.acked_offset > file.sent ||
        (ack.flags & ~3u)) return;
    if (ack.flags & 2) { FinishSend(false); return; }
    if (ack.flags & 1) {
      if (file.last && ack.acked_offset == file.total) FinishSend(true);
      return;
    }
    if (ack.acked_offset <= file.acked) return;
    file.acked = ack.acked_offset;
    file.updated = now;
    Report(file.name, file.acked, file.total, true, file.percent);
  }

  void Receive(const char* data, size_t size, Clock::time_point now = Clock::now()) {
    FileChunkView chunk;
    if (!DecodeFileChunk(data, size, &chunk)) return;
    const auto& h = chunk.header;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = incoming_.find(h.file_id);
    const bool first = h.flags & 1, last = h.flags & 2;
    const bool invalid = (h.flags & ~3u) || h.total_size > INT64_MAX ||
        (first && h.offset != 0) || chunk.payload_size > kFileChunkSize ||
        size != sizeof(FileChunkHeader) + h.name_len + chunk.payload_size ||
        (!first && h.name_len) || (!chunk.payload_size && !last) ||
        last != (h.offset + chunk.payload_size == h.total_size);
    if (invalid || (it == incoming_.end() && incoming_.size() >= 4)) {
      Reject(h.file_id, h.total_size, h.offset);
      if (it != incoming_.end()) { progress_(it->second.name, -1, false, ""); Remove(it->second); incoming_.erase(it); }
      return;
    }
    if (it == incoming_.end()) {
      Incoming file;
      file.name = SafeName(chunk.file_name);
      file.total = h.total_size;
      std::error_code ec;
      if (!directory_.empty()) std::filesystem::create_directories(directory_, ec);
      // A per-transfer directory avoids overwrites, even for duplicate names.
      auto folder = directory_ / (std::to_string(h.file_id) + "-" + std::to_string(++receive_id_));
      if (directory_.empty() || ec || !std::filesystem::create_directory(folder, ec)) {
        Reject(h.file_id, h.total_size, 0); progress_(file.name, -1, false, ""); return;
      }
      file.path = folder / file.name;
      file.partial = folder / ".partial";
      file.output.open(file.partial, std::ios::binary | std::ios::trunc);
      if (!file.output) {
        Reject(h.file_id, h.total_size, 0); progress_(file.name, -1, false, ""); Remove(file); return;
      }
      it = incoming_.emplace(h.file_id, std::move(file)).first;
    }
    auto& file = it->second;
    if (h.total_size != file.total || (first && file.first) ||
        !AddRange(file, h.offset, h.offset + chunk.payload_size)) {
      Reject(h.file_id, h.total_size, h.offset);
      progress_(file.name, -1, false, ""); Remove(file); incoming_.erase(it); return;
    }
    if (first) {
      file.first = true;
      file.name = SafeName(chunk.file_name);
      file.path = file.path.parent_path() / file.name;
    }
    file.last |= last;
    // KCP removes messages in order, but delivers batches outside its lock on
    // both RTP and timer threads. Track coverage instead of assuming callbacks
    // arrive in order; ACK only the contiguous prefix and never expose holes.
    file.received = !file.ranges.empty() && file.ranges.begin()->first == 0
        ? file.ranges.begin()->second : 0;
    const bool complete = file.first && file.last && file.received == file.total;
    file.output.seekp(static_cast<std::streamoff>(h.offset));
    file.output.write(chunk.payload, chunk.payload_size);
    if (complete) { file.output.flush(); file.output.close(); }
    std::error_code ec;
    if (complete && !file.output.fail()) std::filesystem::rename(file.partial, file.path, ec);
    if (file.output.fail() || ec) {
      Reject(h.file_id, h.total_size, h.offset);
      progress_(file.name, -1, false, ""); Remove(file); incoming_.erase(it); return;
    }
    file.updated = now;
    const FileTransferAck ack{kFileAckMagic, h.file_id, file.received, file.total, complete ? 1u : 0u};
    const auto packet = EncodeFileTransferAck(ack);
    send_(kFileFeedbackStream, packet.data(), packet.size());
    if (complete) {
      progress_(file.name, 1, false, file.path.string());
      incoming_.erase(it);
    } else Report(file.name, file.received, file.total, false, file.percent);
  }

  bool Sending() {
    std::lock_guard<std::mutex> lock(mutex_);
    return outgoing_.has_value();
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
    std::filesystem::path path, partial;
    std::ofstream output;
    uint64_t total = 0, received = 0;
    bool first = false, last = false;
    std::map<uint64_t, uint64_t> ranges;
    int percent = -1;
    Clock::time_point updated;
  };
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
  static std::string SafeName(std::string name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    for (auto& ch : name) if (static_cast<unsigned char>(ch) < 32 || ch == ':') ch = '_';
    if (name.empty() || name == "." || name == ".." || name == ".partial" || name.size() > 240) return "received_file";
    return name;
  }
  static void Remove(Incoming& file) {
    file.output.close();
    std::error_code ec;
    std::filesystem::remove(file.partial, ec);
    std::filesystem::remove(file.path.parent_path(), ec);
  }
  void Reject(uint32_t id, uint64_t total, uint64_t offset) {
    const auto packet = EncodeFileTransferAck({kFileAckMagic, id, offset, total, 2});
    send_(kFileFeedbackStream, packet.data(), packet.size());
  }
  void Report(const std::string& name, uint64_t offset, uint64_t total, bool sending, int& previous) {
    const double progress = total ? static_cast<double>(offset) / total : 0;
    const int percent = static_cast<int>(progress * 100);
    if (percent != previous) { previous = percent; progress_(name, std::min(progress, .99), sending, ""); }
  }
  void FinishSend(bool success) {
    progress_(outgoing_->name, success ? 1 : -1, true, "");
    outgoing_->input.close();
    std::error_code ec;
    std::filesystem::remove(outgoing_->path, ec);
    outgoing_.reset();
  }
  const std::chrono::seconds timeout_{30};
  std::mutex mutex_;
  Send send_;
  Progress progress_;
  std::filesystem::path directory_;
  std::optional<Outgoing> outgoing_;
  std::map<uint32_t, Incoming> incoming_;
  uint32_t next_id_ = 0;
  uint64_t receive_id_ = 0;
};
}  // namespace crossdesk::android_controller

#endif