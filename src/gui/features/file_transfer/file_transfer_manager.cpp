#include "features/file_transfer/file_transfer_manager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>

#include "file_transfer.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

FileTransferManager::FileTransferManager(GuiRuntime &owner) : owner_(owner) {}

FileTransferManager::FileTransferState &FileTransferManager::global_state() {
  return global_state_;
}

FileTransferManager::FileTransferState &FileTransferManager::state_for(
    const std::shared_ptr<RemoteSession> &props) {
  return props ? props->file_transfer_ : global_state_;
}

void FileTransferManager::ProcessSelectedFile(
    const std::string &path,
    const std::shared_ptr<RemoteSession> &props,
    const std::string &file_label, const std::string &remote_id) {
  if (path.empty()) {
    return;
  }

  FileTransferState &state = state_for(props);
  LOG_INFO("Selected file: {}", path.c_str());

  const std::filesystem::path file_path = std::filesystem::u8path(path);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file_path, ec)) {
    LOG_ERROR("Selected path is not a regular file: {}", path);
    return;
  }
  const uint64_t file_size = std::filesystem::file_size(file_path, ec);
  if (ec) {
    LOG_ERROR("Failed to get file size: {}", ec.message());
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state.file_transfer_list_mutex_);
    FileTransferState::FileTransferInfo info;
    info.file_name = file_path.filename().u8string();
    info.file_path = file_path;
    info.file_size = file_size;
    info.status = FileTransferState::FileTransferStatus::Queued;
    state.file_transfer_list_.push_back(std::move(info));
  }
  state.file_transfer_window_visible_ = true;

  auto enqueue = [&]() {
    size_t queue_size = 0;
    {
      std::lock_guard<std::mutex> lock(state.file_queue_mutex_);
      state.file_send_queue_.push(
          FileTransferState::QueuedFile{file_path, file_label, remote_id});
      queue_size = state.file_send_queue_.size();
    }
    LOG_INFO("File added to queue: {} ({} files in queue)",
             file_path.filename().string().c_str(), queue_size);
  };

  if (state.file_sending_.load()) {
    enqueue();
    return;
  }

  Start(props, file_path, file_label, remote_id);
  if (!state.file_sending_.load()) {
    enqueue();
  }
}

void FileTransferManager::Start(
    std::shared_ptr<RemoteSession> props,
    const std::filesystem::path &file_path, const std::string &file_label,
    const std::string &remote_id) {
  const bool is_global = !props;
  PeerPtr *peer = is_global ? owner_.peer_ : props->peer_;
  if (!peer) {
    LOG_ERROR("StartFileTransfer: invalid peer");
    return;
  }

  FileTransferState &initial_state = state_for(props);
  bool expected = false;
  if (!initial_state.file_sending_.compare_exchange_strong(expected, true)) {
    LOG_WARN("StartFileTransfer called while another file is active: {}",
             file_path.filename().string().c_str());
    return;
  }

  const auto props_weak = std::weak_ptr<RemoteSession>(props);
  std::thread([this, peer, file_path, file_label, props_weak, remote_id,
               is_global]() {
    auto props_locked = props_weak.lock();
    FileTransferState *state = nullptr;
    if (props_locked) {
      state = &props_locked->file_transfer_;
    } else if (is_global) {
      state = &global_state_;
    } else {
      return;
    }

    std::error_code ec;
    const uint64_t total_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
      LOG_ERROR("Failed to get file size: {}", ec.message().c_str());
      state->file_sending_ = false;
      return;
    }

    state->file_sent_bytes_ = 0;
    state->file_total_bytes_ = total_size;
    state->file_send_rate_bps_ = 0;
    state->file_transfer_window_visible_ = true;
    {
      std::lock_guard<std::mutex> lock(state->file_transfer_mutex_);
      state->file_send_start_time_ = std::chrono::steady_clock::now();
      state->file_send_last_update_time_ = state->file_send_start_time_;
      state->file_send_last_bytes_ = 0;
    }

    FileSender sender;
    const uint32_t file_id = FileSender::NextFileId();
    if (props_locked) {
      std::lock_guard<std::shared_mutex> lock(file_id_to_props_mutex_);
      file_id_to_props_[file_id] = props_weak;
    } else {
      std::lock_guard<std::shared_mutex> lock(file_id_to_state_mutex_);
      file_id_to_state_[file_id] = state;
    }
    state->current_file_id_ = file_id;

    {
      std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
      for (auto &info : state->file_transfer_list_) {
        if (info.file_path == file_path &&
            info.status == FileTransferState::FileTransferStatus::Queued) {
          info.status = FileTransferState::FileTransferStatus::Sending;
          info.file_id = file_id;
          info.file_size = total_size;
          info.sent_bytes = 0;
          break;
        }
      }
    }

    const int ret = sender.SendFile(
        file_path, file_path.filename().string(),
        [peer, file_label, remote_id](const char *buffer, size_t size) {
          if (remote_id.empty()) {
            return SendReliableDataFrame(peer, buffer, size,
                                         file_label.c_str());
          }
          return SendReliableDataFrameToPeer(
              peer, buffer, size, file_label.c_str(), remote_id.c_str(),
              remote_id.size());
        },
        64 * 1024, file_id);

    if (ret == 0) {
      return;
    }

    state->file_sending_ = false;
    state->file_transfer_window_visible_ = false;
    state->file_sent_bytes_ = 0;
    state->file_total_bytes_ = 0;
    state->file_send_rate_bps_ = 0;
    state->current_file_id_ = 0;
    Unregister(file_id, props_locked != nullptr);

    {
      std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
      for (auto &info : state->file_transfer_list_) {
        if (info.file_id == file_id) {
          info.status = FileTransferState::FileTransferStatus::Failed;
          break;
        }
      }
    }
    LOG_ERROR("FileSender::SendFile failed for [{}], ret={}",
              file_path.string().c_str(), ret);
    ProcessQueue(props_locked);
  }).detach();
}

void FileTransferManager::ProcessQueue(
    std::shared_ptr<RemoteSession> props) {
  FileTransferState &state = state_for(props);
  if (state.file_sending_.load()) {
    return;
  }

  FileTransferState::QueuedFile queued_file;
  {
    std::lock_guard<std::mutex> lock(state.file_queue_mutex_);
    if (state.file_send_queue_.empty()) {
      return;
    }
    queued_file = state.file_send_queue_.front();
    state.file_send_queue_.pop();
  }
  Start(props, queued_file.file_path, queued_file.file_label,
        queued_file.remote_id);
}

void FileTransferManager::Unregister(uint32_t file_id, bool per_peer) {
  if (per_peer) {
    std::lock_guard<std::shared_mutex> lock(file_id_to_props_mutex_);
    file_id_to_props_.erase(file_id);
  } else {
    std::lock_guard<std::shared_mutex> lock(file_id_to_state_mutex_);
    file_id_to_state_.erase(file_id);
  }
}

void FileTransferManager::HandleAck(const char *data, size_t size) {
  if (size < sizeof(FileTransferAck)) {
    LOG_ERROR("FileTransferAck: buffer too small, size={}", size);
    return;
  }

  FileTransferAck ack{};
  std::memcpy(&ack, data, sizeof(ack));
  if (ack.magic != kFileAckMagic) {
    LOG_ERROR("FileTransferAck: invalid magic, got 0x{:08X}", ack.magic);
    return;
  }

  std::shared_ptr<RemoteSession> props;
  {
    std::shared_lock lock(file_id_to_props_mutex_);
    const auto it = file_id_to_props_.find(ack.file_id);
    if (it != file_id_to_props_.end()) {
      props = it->second.lock();
    }
  }

  FileTransferState *state = props ? &props->file_transfer_ : nullptr;
  if (!state) {
    std::shared_lock lock(file_id_to_state_mutex_);
    const auto it = file_id_to_state_.find(ack.file_id);
    if (it != file_id_to_state_.end()) {
      state = it->second;
    }
  }
  if (!state) {
    LOG_WARN("FileTransferAck: no state found for file_id={}", ack.file_id);
    return;
  }

  state->file_sent_bytes_ = ack.acked_offset;
  state->file_total_bytes_ = ack.total_size;
  uint32_t rate_bps = 0;
  if (props) {
    const uint32_t bitrate =
        props->net_traffic_stats_.data_outbound_stats.bitrate;
    if (bitrate > 0 && state->file_sending_.load()) {
      rate_bps = static_cast<uint32_t>(bitrate * 0.99f);
      const uint32_t current_rate = state->file_send_rate_bps_.load();
      if (current_rate > 0) {
        rate_bps = static_cast<uint32_t>(current_rate * 0.7 + rate_bps * 0.3);
      }
    } else {
      rate_bps = state->file_send_rate_bps_.load();
    }
  } else {
    const uint32_t current_rate = state->file_send_rate_bps_.load();
    uint32_t estimated_rate = 0;
    const auto now = std::chrono::steady_clock::now();
    uint64_t last_bytes = 0;
    std::chrono::steady_clock::time_point last_time;
    {
      std::lock_guard<std::mutex> lock(state->file_transfer_mutex_);
      last_bytes = state->file_send_last_bytes_;
      last_time = state->file_send_last_update_time_;
    }
    if (state->file_sending_.load() && ack.acked_offset >= last_bytes) {
      const auto delta_bytes = ack.acked_offset - last_bytes;
      const double seconds =
          std::chrono::duration<double>(now - last_time).count();
      if (seconds > 0.0 && delta_bytes > 0) {
        const double bits_per_second = delta_bytes * 8.0 / seconds;
        estimated_rate = static_cast<uint32_t>((std::min)(
            bits_per_second,
            static_cast<double>((std::numeric_limits<uint32_t>::max)())));
      }
    }
    rate_bps =
        estimated_rate > 0 && current_rate > 0
            ? static_cast<uint32_t>(current_rate * 0.7 + estimated_rate * 0.3)
        : estimated_rate > 0 ? estimated_rate
                             : current_rate;
  }

  state->file_send_rate_bps_ = rate_bps;
  state->file_send_last_bytes_ = ack.acked_offset;
  state->file_send_last_update_time_ = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
    for (auto &info : state->file_transfer_list_) {
      if (info.file_id == ack.file_id) {
        info.sent_bytes = ack.acked_offset;
        info.file_size = ack.total_size;
        info.rate_bps = rate_bps;
        if ((ack.flags & 0x01) != 0) {
          info.status = FileTransferState::FileTransferStatus::Completed;
          info.sent_bytes = ack.total_size;
        }
        break;
      }
    }
  }

  if ((ack.flags & 0x01) == 0) {
    return;
  }
  LOG_INFO("File transfer completed: file_id={}, bytes={}", ack.file_id,
           ack.total_size);
  state->file_transfer_window_visible_ = true;
  state->file_sending_ = false;
  Unregister(ack.file_id, props != nullptr);
  ProcessQueue(props);
}

void FileTransferManager::HandleDropEvent(const SDL_Event &event) {
  if (event.type != SDL_EVENT_DROP_FILE ||
      !((owner_.stream_window_ &&
         SDL_GetWindowID(owner_.stream_window_) == event.window.windowID) ||
        (owner_.server_window_ &&
         SDL_GetWindowID(owner_.server_window_) == event.window.windowID))) {
    return;
  }

  if (owner_.stream_window_ &&
      SDL_GetWindowID(owner_.stream_window_) == event.window.windowID) {
    if (!owner_.stream_window_inited_ || !event.drop.data) {
      return;
    }
    std::shared_lock lock(owner_.remote_sessions_mutex_);
    for (const auto &[remote_id, props] : owner_.remote_sessions_) {
      if (props && props->tab_selected_ && props->peer_) {
        ProcessSelectedFile(static_cast<const char *>(event.drop.data), props,
                            props->file_label_);
        break;
      }
    }
    return;
  }

  if (owner_.server_window_inited_ && event.drop.data) {
    const auto file_path = std::filesystem::u8path(event.drop.data);
    std::error_code ec;
    if (std::filesystem::is_regular_file(file_path, ec)) {
      LOG_INFO("Drop file [{}] on server window (size: {} bytes)",
               event.drop.data, std::filesystem::file_size(file_path, ec));
    }
  }
}

} // namespace crossdesk
