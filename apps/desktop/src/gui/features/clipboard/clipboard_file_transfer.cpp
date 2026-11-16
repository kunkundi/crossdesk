#include "clipboard_file_transfer.h"

#include <nlohmann/json.hpp>
#include <random>

#include "filesystem_utf8.h"

namespace crossdesk {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr uint8_t kOffer = 1, kReady = 2, kChunk = 3, kAck = 4, kCancel = 5, kReject = 6;
constexpr uint8_t kApplied = 7;
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

ClipboardFileTransfer::Files::~Files() {
  std::error_code ec;
  if (!directory.empty()) std::filesystem::remove_all(directory, ec);
}

ClipboardFileTransfer::ClipboardFileTransfer(Send send, Begin begin, Complete complete,
                                             Progress progress)
    : send_(std::move(send)), begin_(std::move(begin)), complete_(std::move(complete)),
      progress_(std::move(progress)) {
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

void ClipboardFileTransfer::Confirm(uint32_t token, bool applied) {
  std::lock_guard lock(mutex_);
  if (stopping_ || confirmations_.size() >= 128) return;
  confirmations_.emplace_back(token, applied);
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
  receive_token_ = 0;
}

void ClipboardFileTransfer::StartCopy(const std::vector<std::string>& paths) {
  if (send_token_) SendPacket(kCancel, send_token_);
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
    list.push_back({{"id", id}, {"size", size}});
    items.push_back({id, value, name, size});
  }
  const auto offer = list.dump();
  if (offer.size() > kMaxOffer) return;
  send_token_ = file_transfer::Session::NextFileId();
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
    SendPacket(kCancel, send_token_); ResetOutgoing(file_transfer::Status::Failed);
  }
}

void ClipboardFileTransfer::ReportOutgoing() {
  if (!outgoing_ || !next_) return;
  auto& item = items_[next_ - 1];
  const auto state = outgoing_->Snapshot();
  // A batch is usable only after every file reaches the OS clipboard.
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
    for (const auto& entry : list) {
      if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_unsigned() ||
          !entry.contains("size") || !entry["size"].is_number_unsigned()) { reject(); return; }
      const auto id = entry["id"].get<uint64_t>(), bytes = entry["size"].get<uint64_t>();
      if (!id || id > UINT32_MAX || bytes > INT64_MAX || !expected.emplace(id, bytes).second) {
        reject(); return;
      }
      order.push_back(static_cast<uint32_t>(id));
    }
    files_ = std::make_shared<Files>();
    files_->directory = MakeCache();
    if (files_->directory.empty()) { ResetIncoming(); reject(); return; }
    receive_token_ = token; revision_ = *revision;
    expected_ = std::move(expected); order_ = std::move(order);
    incoming_ = std::make_unique<file_transfer::Session>(
        [this](const char*, const char* bytes, size_t length) {
          return SendPacket(kAck, receive_token_, bytes, length) ? 0 : -1;
        }, [this](const auto& name, double fraction, bool sending, const std::string& path) {
          if (!sending && fraction == 1 && !path.empty()) received_[completed_id_] = path;
        }, file_transfer::Options{false, true, 1},
        [this](const file_transfer::ReceiveState& state) { completed_id_ = state.id; });
    incoming_->SetDirectory(files_->directory);
    receive_activity_ = Clock::now();
    if (!SendPacket(kReady, token)) ResetIncoming();
  } else if (type == kReady && !size && token == send_token_ && !ready_) {
    ready_ = true; send_activity_ = Clock::now(); StartNext();
  } else if (type == kChunk && incoming_ && token == receive_token_) {
    FileChunkView chunk;
    if (!DecodeFileChunk(data, size, &chunk)) return;
    const auto found = expected_.find(chunk.header.file_id);
    if (found == expected_.end() || found->second != chunk.header.total_size ||
        received_.count(chunk.header.file_id)) return;
    receive_activity_ = Clock::now();
    if (!incoming_->Receive(data, size)) {
      SendPacket(kReject, token); ResetIncoming(); return;
    }
    if (received_.size() == expected_.size()) {
      for (auto id : order_) files_->paths.push_back(received_.at(id));
      complete_(files_, revision_, receive_token_);
      ResetIncoming();
    }
  } else if (type == kAck && outgoing_ && token == send_token_) {
    outgoing_->Ack(data, size); send_activity_ = Clock::now();
  } else if (type == kCancel && !size) {
    if (!latest_receive_token_ || static_cast<int32_t>(token - latest_receive_token_) > 0)
      latest_receive_token_ = token;
    if (token == receive_token_) ResetIncoming();
    if (current_receive_token_ == token) current_receive_token_ = 0;
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
      std::deque<std::pair<uint32_t, bool>> confirmations;
      {
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(5),
            [this] { return stopping_ || copy_.has_value() || !packets_.empty() || !confirmations_.empty(); });
        if (stopping_) break;
        packets.swap(packets_); queued_bytes_ = 0; copy.swap(copy_);
        confirmations.swap(confirmations_);
      }
      if (copy) StartCopy(*copy);
      for (const auto& [token, applied] : confirmations)
        SendPacket(applied ? kApplied : kReject, token);
      for (const auto& packet : packets) Handle(packet);
      if (incoming_) incoming_->Pump();
      if (outgoing_) {
        // Fill the engine's bounded ACK window without limiting throughput to
        // one 64 KiB chunk per UI tick.
        for (int i = 0; i < 16 && outgoing_->Pump(); ++i) {}
        const auto state = outgoing_->Snapshot();
        ReportOutgoing();
        if (state.status == file_transfer::Status::Completed) StartNext();
        else if (state.status != file_transfer::Status::Sending) {
          SendPacket(kCancel, send_token_); ResetOutgoing(file_transfer::Status::Failed);
        }
      }
      const auto now = Clock::now();
      if (send_token_ && now - send_activity_ > kTimeout) {
        SendPacket(kCancel, send_token_); ResetOutgoing(file_transfer::Status::Failed);
      }
      if (receive_token_ && now - receive_activity_ > kTimeout) {
        SendPacket(kReject, receive_token_); ResetIncoming();
      }
    }
  } catch (const std::exception&) {
    // Filesystem/JSON failures must never terminate the desktop process.
  }
  ResetOutgoing(); ResetIncoming();
  current_receive_token_ = 0;
  { std::lock_guard lock(mutex_); stopping_ = true; }
}

}  // namespace crossdesk
