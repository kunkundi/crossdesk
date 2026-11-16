#include "clipboard_file_transfer.h"

#include <nlohmann/json.hpp>
#include <random>

#include "filesystem_utf8.h"

namespace crossdesk {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
// Version 2 offers are deliberately distinct from the eager-transfer protocol.
constexpr uint8_t kOffer = 8, kReady = 2, kChunk = 3, kAck = 4, kCancel = 5, kReject = 6;
constexpr uint8_t kApplied = 7, kFailed = 9;
constexpr size_t kHeader = 5, kMaxFiles = 128, kMaxOffer = 60000;
constexpr auto kTimeout = std::chrono::seconds(60);

std::string Utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}

std::filesystem::path MakeCache() {
  std::error_code ec;
  const auto temp = std::filesystem::temp_directory_path(ec);
  if (ec) return {};
  std::random_device random;
  for (int attempt = 0; attempt < 20; ++attempt) {
    auto path = temp / ("crossdesk-clipboard-" + std::to_string(random()) +
                       "-" + std::to_string(random()));
    if (!std::filesystem::create_directory(path, ec)) continue;
    std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    if (!ec) return path;
    std::filesystem::remove(path, ec);
  }
  return {};
}
}  // namespace

ClipboardFileTransfer::ClipboardFileTransfer(Send send, Begin begin, Offer offer,
                                             Progress progress, OnReceiveProgress receive_progress)
    : send_(std::move(send)), begin_(std::move(begin)), offer_(std::move(offer)),
      progress_(std::move(progress)), receive_progress_(std::move(receive_progress)) {
  worker_ = std::thread([this] { Run(); });
}

ClipboardFileTransfer::~ClipboardFileTransfer() { Stop(); }

void ClipboardFileTransfer::Stop() {
  std::lock_guard stop_lock(stop_mutex_);
  { std::lock_guard lock(mutex_); stopping_ = true; }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void ClipboardFileTransfer::Copy(std::vector<std::string> paths) {
  std::lock_guard lock(mutex_);
  if (stopping_) return;
  copy_ = std::move(paths);
  wake_.notify_all();
}

void ClipboardFileTransfer::Receive(const char* data, size_t size) {
  if (!data || size < kHeader || size > kFileChunkSize + 1024) return;
  std::lock_guard lock(mutex_);
  if (stopping_ || queued_bytes_ + size > 4 * 1024 * 1024 || packets_.size() >= 256) return;
  packets_.emplace_back(data, size);
  queued_bytes_ += size;
  wake_.notify_all();
}

void ClipboardFileTransfer::Paste(uint32_t token) {
  std::lock_guard lock(mutex_);
  if (stopping_ || !Current(token)) return;
  paste_ = token;
  wake_.notify_all();
}

void ClipboardFileTransfer::CancelReceive(uint32_t token) {
  // Invalidate immediately so a cancelled offer cannot start a queued paste.
  auto expected = token;
  current_receive_token_.compare_exchange_strong(expected, 0);
  std::lock_guard lock(mutex_);
  if (stopping_ || cancellations_.size() >= 128) return;
  cancellations_.push_back(token);
  wake_.notify_all();
}

bool ClipboardFileTransfer::SendPacket(uint8_t type, uint32_t token, const char* data, size_t size) {
  std::string packet(kHeader, '\0');
  packet[0] = static_cast<char>(type);
  for (int i = 0; i < 4; ++i) packet[i + 1] = static_cast<char>(token >> (i * 8));
  if (size) packet.append(data, size);
  return send_(packet);
}

void ClipboardFileTransfer::ResetOutgoing(file_transfer::Status status) {
  outgoing_.reset();
  for (const auto& item : items_)
    if (progress_ && item.status != file_transfer::Status::Completed)
      progress_(item.id, item.name, item.size, 0, status);
  items_.clear(); next_ = 0; ready_ = false; applied_ = false; send_token_ = 0;
}

void ClipboardFileTransfer::ResetIncoming() {
  incoming_.reset(); files_.reset(); expected_.clear(); received_.clear(); order_.clear();
  received_bytes_.clear();
  receive_token_ = 0;
}

void ClipboardFileTransfer::FinishReceive(file_transfer::ReceiveStatus status) {
  if (status != file_transfer::ReceiveStatus::Completed && offered_) offered_->Cancel();
  if (!receiving_) return;
  receiving_->status = status;
  if (status != file_transfer::ReceiveStatus::Completed) {
    auto expected = receiving_->token;
    current_receive_token_.compare_exchange_strong(expected, 0);
  }
  if (receive_progress_) receive_progress_(*receiving_);
  receiving_.reset();
}

void ClipboardFileTransfer::StartCopy(const std::vector<std::string>& paths) {
  if (offered_send_token_) SendPacket(kCancel, offered_send_token_);
  offered_send_token_ = 0;
  ResetOutgoing();
  if (paths.empty() || paths.size() > kMaxFiles) return;
  Json list = Json::array();
  std::vector<Item> items;
  for (const auto& value : paths) {
    const auto path = PathFromUtf8(value);
    std::error_code ec;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, ec)) return;
    const auto size = std::filesystem::file_size(path, ec);
    const auto name = Utf8(path.filename());
    if (ec || size > INT64_MAX || name.empty() || name.size() > 240) return;
    const auto id = file_transfer::Session::NextFileId();
    list.push_back({{"id", id}, {"name", name}, {"size", size}});
    items.push_back({id, value, name, size});
  }
  const auto offer = list.dump();
  if (offer.size() > kMaxOffer) return;
  send_token_ = file_transfer::Session::NextFileId();
  offered_send_token_ = send_token_;
  items_ = std::move(items);
  for (const auto& item : items_)
    if (progress_) progress_(item.id, item.name, item.size, 0, file_transfer::Status::Idle);
  send_activity_ = Clock::now();
  if (!SendPacket(kOffer, send_token_, offer.data(), offer.size()))
    ResetOutgoing(file_transfer::Status::Failed);
}

void ClipboardFileTransfer::StartNext() {
  if (next_ == items_.size()) {
    outgoing_.reset(); send_activity_ = Clock::now();
    if (applied_) FinishApplied();
    return;
  }
  const auto& item = items_[next_++];
  outgoing_ = std::make_unique<file_transfer::Session>(
      [this](const char*, const char* data, size_t size) {
        return SendPacket(kChunk, send_token_, data, size) ? 0 : -1;
      }, file_transfer::Session::Progress{});
  if (!outgoing_->Start(PathFromUtf8(item.path), item.name, Clock::now(), item.id) ||
      outgoing_->Snapshot().total != item.size) {
    SendPacket(kFailed, send_token_); ResetOutgoing(file_transfer::Status::Failed);
  }
}

void ClipboardFileTransfer::ReportOutgoing() {
  if (!outgoing_ || !next_) return;
  auto& item = items_[next_ - 1];
  const auto state = outgoing_->Snapshot();
  // Native paste consumers wait for the complete batch.
  item.status = state.status == file_transfer::Status::Completed
      ? file_transfer::Status::Sending : state.status;
  if (progress_) progress_(item.id, item.name, state.total, state.acknowledged, item.status);
}

void ClipboardFileTransfer::FinishApplied() {
  if (items_.empty()) return;
  for (auto& item : items_) {
    item.status = file_transfer::Status::Completed;
    if (progress_) progress_(item.id, item.name, item.size, item.size, item.status);
  }
  ResetOutgoing();
}

void ClipboardFileTransfer::StartReceive(uint32_t token) {
  if (!Current(token) || token != receive_token_ || incoming_ || !offered_ || !offered_->valid()) return;
  uint64_t total = 0;
  for (const auto& entry : offered_->entries()) total += entry.size;
  receiving_ = ReceiveProgress{token, expected_.size(), 0, total};
  if (receive_progress_) receive_progress_(*receiving_);
  files_ = std::make_shared<Files>();
  files_->directory = MakeCache();
  if (files_->directory.empty()) {
    FinishReceive(file_transfer::ReceiveStatus::Failed);
    SendPacket(kReject, token); ResetIncoming(); return;
  }
  incoming_ = std::make_unique<file_transfer::Session>(
      [this](const char*, const char* bytes, size_t length) {
        return SendPacket(kAck, receive_token_, bytes, length) ? 0 : -1;
      }, [this](const auto& name, double fraction, bool sending, const std::string& path) {
        if (!sending && fraction == 1 && !path.empty()) received_[completed_id_] = path;
      }, file_transfer::Options{false, true, 1},
      [this](const file_transfer::ReceiveState& state) {
        completed_id_ = state.id;
        if (!receiving_) return;
        received_bytes_[state.id] = state.received;
        receiving_->received = 0;
        for (const auto& [_, bytes] : received_bytes_) receiving_->received += bytes;
        receiving_->name = state.name;
        receiving_->completed_files = received_.size() +
            (state.status == file_transfer::ReceiveStatus::Completed ? 1 : 0);
        if (state.status == file_transfer::ReceiveStatus::Failed ||
            state.status == file_transfer::ReceiveStatus::Cancelled) FinishReceive(state.status);
        else if (receive_progress_) receive_progress_(*receiving_);
      });
  incoming_->SetDirectory(files_->directory);
  receive_activity_ = Clock::now();
  if (!SendPacket(kReady, token)) {
    FinishReceive(file_transfer::ReceiveStatus::Failed); ResetIncoming();
  }
}

void ClipboardFileTransfer::Handle(const std::string& packet) {
  const auto type = static_cast<uint8_t>(packet[0]);
  uint32_t token = 0;
  for (int i = 0; i < 4; ++i) token |= uint32_t(uint8_t(packet[i + 1])) << (i * 8);
  if (!token) return;
  const auto* data = packet.data() + kHeader;
  const auto size = packet.size() - kHeader;
  if (type == kOffer) {
    if (latest_receive_token_ && static_cast<int32_t>(token - latest_receive_token_) <= 0) return;
    latest_receive_token_ = token;
    FinishReceive(file_transfer::ReceiveStatus::Cancelled);
    current_receive_token_ = token;
    ResetIncoming();
    const auto reject = [&] { current_receive_token_ = 0; SendPacket(kReject, token); };
    const auto revision = begin_();
    const auto list = size <= kMaxOffer ? Json::parse(data, data + size, nullptr, false) : Json{};
    if (!revision || !list.is_array() || list.empty() || list.size() > kMaxFiles) {
      reject(); return;
    }
    std::map<uint32_t, uint64_t> expected;
    std::vector<uint32_t> order;
    std::vector<ClipboardFilePromise::Entry> entries;
    uint64_t total = 0;
    for (const auto& entry : list) {
      if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_unsigned() ||
          !entry.contains("size") || !entry["size"].is_number_unsigned() ||
          !entry.contains("name") || !entry["name"].is_string()) { reject(); return; }
      const auto id = entry["id"].get<uint64_t>(), bytes = entry["size"].get<uint64_t>();
      if (!id || id > UINT32_MAX || bytes > INT64_MAX || bytes > UINT64_MAX - total ||
          !expected.emplace(static_cast<uint32_t>(id), bytes).second) {
        reject(); return;
      }
      const auto name = entry["name"].get<std::string>();
      if (name.empty() || name.size() > 240 || name == "." || name == ".." ||
          name.find_first_of("/\\") != std::string::npos || name.find('\0') != std::string::npos) {
        reject(); return;
      }
      entries.push_back({static_cast<uint32_t>(id), name, bytes});
      order.push_back(static_cast<uint32_t>(id));
      total += bytes;
    }
    receive_token_ = token;
    expected_ = std::move(expected); order_ = std::move(order);
    offered_token_ = token;
    offered_ = std::make_shared<ClipboardFilePromise>(std::move(entries));
    offer_(offered_, *revision, token);
  } else if (type == kReady && !size && token == send_token_ && !ready_) {
    ready_ = true; send_activity_ = Clock::now(); StartNext();
  } else if (type == kChunk && incoming_ && token == receive_token_) {
    FileChunkView chunk;
    if (!DecodeFileChunk(data, size, &chunk)) return;
    const auto found = expected_.find(chunk.header.file_id);
    if (found == expected_.end() || found->second != chunk.header.total_size ||
        received_.count(chunk.header.file_id)) return;
    const auto& entries = offered_->entries();
    const auto entry = std::find_if(entries.begin(), entries.end(),
        [&](const auto& value) { return value.id == chunk.header.file_id; });
    if (chunk.header.flags & 0x01) {
      if (entry == entries.end() || chunk.file_name != entry->name) {
        FinishReceive(file_transfer::ReceiveStatus::Failed);
        SendPacket(kReject, token); ResetIncoming(); return;
      }
    }
    receive_activity_ = Clock::now();
    if (!incoming_->Receive(data, size)) {
      FinishReceive(file_transfer::ReceiveStatus::Failed);
      SendPacket(kReject, token); ResetIncoming(); return;
    }
    if (received_.size() == expected_.size()) {
      if (!Current(token) || !offered_->valid()) {
        FinishReceive(file_transfer::ReceiveStatus::Cancelled);
        SendPacket(kReject, token); ResetIncoming(); return;
      }
      for (auto id : order_) files_->paths.push_back(received_.at(id));
      offered_->Resolve(files_);
      SendPacket(kApplied, receive_token_);
      FinishReceive(file_transfer::ReceiveStatus::Completed);
      ResetIncoming();
    }
  } else if (type == kAck && outgoing_ && token == send_token_) {
    outgoing_->Ack(data, size); send_activity_ = Clock::now();
  } else if ((type == kCancel || type == kFailed) && !size) {
    if (!latest_receive_token_ || static_cast<int32_t>(token - latest_receive_token_) > 0)
      latest_receive_token_ = token;
    if (token == receive_token_) ResetIncoming();
    if (receiving_ && receiving_->token == token)
      FinishReceive(type == kFailed ? file_transfer::ReceiveStatus::Failed
                                    : file_transfer::ReceiveStatus::Cancelled);
    if (current_receive_token_ == token) {
      current_receive_token_ = 0;
      if (offered_) offered_->Cancel();
    }
  } else if (type == kReject && !size && token == send_token_) {
    ResetOutgoing(file_transfer::Status::Failed);
  } else if (type == kApplied && !size && token == send_token_) {
    applied_ = true;
    if (!outgoing_ && ready_ && next_ == items_.size()) FinishApplied();
  }
}

void ClipboardFileTransfer::Run() {
  try {
    for (;;) {
      std::deque<std::string> packets;
      std::optional<std::vector<std::string>> copy;
      uint32_t paste = 0;
      std::deque<uint32_t> cancellations;
      {
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(5),
            [this] { return stopping_ || copy_.has_value() || !packets_.empty() || !cancellations_.empty() || paste_; });
        if (stopping_) break;
        packets.swap(packets_); queued_bytes_ = 0; copy.swap(copy_);
        cancellations.swap(cancellations_);
        paste = std::exchange(paste_, 0);
      }
      if (copy) StartCopy(*copy);
      for (auto token : cancellations) {
        SendPacket(kReject, token);
        if (token == offered_token_) {
          FinishReceive(file_transfer::ReceiveStatus::Cancelled);
          ResetIncoming();
        }
      }
      for (const auto& packet : packets) Handle(packet);
      if (paste) StartReceive(paste);
      if (incoming_) incoming_->Pump();
      if (outgoing_) {
        // Fill the engine's bounded ACK window without limiting throughput to
        // one 64 KiB chunk per UI tick.
        for (int i = 0; i < 16 && outgoing_->Pump(); ++i) {}
        const auto state = outgoing_->Snapshot();
        ReportOutgoing();
        if (state.status == file_transfer::Status::Completed) StartNext();
        else if (state.status != file_transfer::Status::Sending) {
          SendPacket(kFailed, send_token_); ResetOutgoing(file_transfer::Status::Failed);
        }
      }
      const auto now = Clock::now();
      if (send_token_ && ready_ && now - send_activity_ > kTimeout) {
        SendPacket(kFailed, send_token_); ResetOutgoing(file_transfer::Status::Failed);
      }
      if (receiving_ && now - receive_activity_ > kTimeout) {
        SendPacket(kReject, receiving_->token);
        FinishReceive(file_transfer::ReceiveStatus::Failed); ResetIncoming();
      }
    }
  } catch (const std::exception&) {
    // Filesystem/JSON failures must never terminate the desktop process.
  }
  FinishReceive(file_transfer::ReceiveStatus::Cancelled);
  ResetOutgoing(); ResetIncoming();
  current_receive_token_ = 0;
  { std::lock_guard lock(mutex_); stopping_ = true; }
}

}  // namespace crossdesk
