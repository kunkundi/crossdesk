#include "controller_files.h"
#include "file_browser.h"
#include "file_transfer_core.h"
#include <deque>
#include <filesystem>
#include <mutex>
#include <set>
#include <stdexcept>

namespace crossdesk {
namespace fs = std::filesystem;
using nlohmann::json;
namespace {
std::string Utf8(const fs::path &path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}
bool Terminal(const std::string &state) {
  return state == "completed" || state == "failed" || state == "cancelled";
}
}
struct ControllerFiles::State {
  Send send;
  std::mutex mutex;
  std::deque<std::pair<std::string, std::string>> output;
  size_t output_bytes = 0;
  struct Upload { uint32_t id; std::string path; };
  std::deque<Upload> uploads;
  std::map<uint32_t, json> files, listings;
  struct Route { std::string peer; std::chrono::steady_clock::time_point expires; };
  std::map<uint32_t, Route> routes;
  std::map<std::string, json> history;
  std::map<std::string, std::string> arguments;
  std::string operation, state = "idle", destination;
  bool download = false, prepared = false;
  uint32_t sending = 0, completed_receive = 0;
  std::unique_ptr<file_transfer::Session> transfer;
  std::unique_ptr<FileBrowser> browser;

  explicit State(Send callback) : send(std::move(callback)) {
    transfer = std::make_unique<file_transfer::Session>(
        [this](const char *stream, const char *data, size_t size) {
          return Queue(stream, std::string(data, size)) ? 0 : -1;
        },
        [this](const std::string &, double progress, bool outbound, const std::string &path) {
          std::lock_guard lock(mutex);
          if (outbound && sending && files.count(sending)) {
            auto &file = files.at(sending);
            const auto total = file.value("total", uint64_t{});
            file["bytes"] = progress < 0 ? file.value("bytes", uint64_t{}) : uint64_t(progress * total);
            file["state"] = progress < 0 ? "failed" : progress >= 1 ? "completed" : "transferring";
          }
          if (!outbound && !path.empty() && completed_receive) {
            if (files.count(completed_receive)) files[completed_receive]["local_path"] = path;
            completed_receive = 0;
          }
        }, file_transfer::Options{false, false, 32},
        [this](const file_transfer::ReceiveState &progress) {
          std::lock_guard lock(mutex);
          auto found = files.find(progress.id);
          if (found == files.end()) return;
          auto &file = found->second;
          if (progress.status == file_transfer::ReceiveStatus::Completed) completed_receive = progress.id;
          file["name"] = progress.name;
          file["total"] = progress.total;
          file["bytes"] = progress.received;
          file["state"] = progress.status == file_transfer::ReceiveStatus::Completed ? "completed"
              : progress.status == file_transfer::ReceiveStatus::Cancelled ? "cancelled"
              : progress.status == file_transfer::ReceiveStatus::Failed ? "failed" : "transferring";
        });
    browser = std::make_unique<FileBrowser>(
        [this](const std::string &peer, const json &message) {
          auto packet = message;
          if (packet.value("op", "") != "reply" && packet.value("op", "") != "notice") {
            std::lock_guard lock(mutex);
            if (routes.size() >= 512) return false;
            routes[packet.at("id").get<uint32_t>()] = {peer,
                std::chrono::steady_clock::now() + std::chrono::seconds(30)};
            if (peer != "remote") packet["transfer_id"] = peer;
          }
          return Queue(kControlStream, packet.dump());
        },
        [this](const std::string &peer, const std::string &path, uint32_t id) {
          std::error_code error;
          const auto total = fs::file_size(fs::u8path(path), error);
          std::lock_guard lock(mutex);
          if (error || peer != operation || Terminal(state) || files.size() >= 4096) return false;
          files[id] = {{"file_id", id}, {"name", Utf8(fs::u8path(path).filename())},
              {"direction", "upload"}, {"total", total}, {"bytes", 0}, {"state", "queued"}};
          uploads.push_back({id, path});
          return true;
        },
        [this](const std::string &peer, uint32_t id, const fs::path &directory) {
          {
            std::lock_guard lock(mutex);
            if (!download || peer != operation || Terminal(state) || files.size() >= 4096) return false;
          }
          if (!transfer->ReserveDirectory(id, directory)) return false;
          {
            std::lock_guard lock(mutex);
            if (peer == operation && !Terminal(state)) {
              files[id] = {{"file_id", id}, {"direction", "download"}, {"total", 0},
                  {"bytes", 0}, {"state", "queued"}, {"directory", Utf8(directory)}};
              return true;
            }
          }
          transfer->CancelReceive(id);
          return false;
        });
  }
  ~State() { browser->Stop(); transfer->Cancel(); }
  bool Queue(const char *stream, std::string message) {
    std::lock_guard lock(mutex);
    if (output.size() >= 256 || output_bytes + message.size() > 2 * 1024 * 1024) return false;
    output_bytes += message.size();
    output.emplace_back(stream, std::move(message));
    return true;
  }
  json Snapshot(size_t offset = 0, size_t limit = 256) {
    json result{{"operation_id", operation}, {"state", state}, {"files", json::array()},
        {"total_files", files.size()}, {"offset", offset}, {"prepared", prepared}};
    size_t index = 0;
    for (const auto &[id, file] : files) {
      if (index++ < offset) continue;
      if (result["files"].size() >= limit) break;
      result["files"].push_back(file);
    }
    result["next_offset"] = offset + result["files"].size() < files.size()
        ? json(offset + result["files"].size()) : json(nullptr);
    return result;
  }
};

ControllerFiles::ControllerFiles(Send send) : state_(std::make_unique<State>(std::move(send))) {}
ControllerFiles::~ControllerFiles() = default;

json ControllerFiles::Command(const json &request) {
  auto &s = *state_;
  for (const auto *key : {"offset", "request_id"})
    if (request.contains(key) && (!request[key].is_number_unsigned() || request[key].get<uint64_t>() > UINT32_MAX))
      throw std::invalid_argument("INVALID_ARGUMENT");
  for (const auto *key : {"path", "destination", "operation_id"}) {
    if (!request.contains(key)) continue;
    if (!request[key].is_string()) throw std::invalid_argument("INVALID_ARGUMENT");
    const auto text = request[key].get<std::string>();
    if (text.size() > 4096 || text.find('\0') != std::string::npos) throw std::invalid_argument("INVALID_ARGUMENT");
  }
  const auto command = request.at("operation").get<std::string>();
  if (command == "list") {
    {
      std::lock_guard lock(s.mutex);
      for (auto it = s.listings.begin(); it != s.listings.end() && s.listings.size() >= 32;) {
        if (it->second.value("state", "") != "pending") it = s.listings.erase(it);
        else ++it;
      }
      if (s.listings.size() >= 32) throw std::invalid_argument("FILE_REQUEST_LIMIT");
    }
    const auto id = s.browser->List("remote", request.value("path", ""), false);
    std::lock_guard lock(s.mutex);
    s.listings[id] = {{"state", "pending"}};
    return {{"request_id", id}, {"state", "pending"}};
  }
  if (command == "list_result") {
    std::lock_guard lock(s.mutex);
    const auto id = request.at("request_id").get<uint32_t>();
    const auto found = s.listings.find(id);
    if (found == s.listings.end()) throw std::invalid_argument("FILE_REQUEST_NOT_FOUND");
    auto result = found->second;
    if (!result.contains("entries")) return result;
    const auto &entries = found->second.at("entries");
    const size_t offset = request.value("offset", size_t{});
    if (offset > entries.size()) throw std::invalid_argument("INVALID_ARGUMENT");
    result["entries"] = json::array();
    for (size_t i = offset; i < std::min(offset + 256, entries.size()); ++i)
      result["entries"].push_back(entries[i]);
    result["next_offset"] = offset + result["entries"].size() < entries.size()
        ? json(offset + result["entries"].size()) : json(nullptr);
    result["request_id"] = id;
    return result;
  }
  if (command == "list_cancel") {
    const auto id = request.at("request_id").get<uint32_t>();
    s.browser->CancelList("remote", id);
    std::lock_guard lock(s.mutex);
    s.listings.erase(id);
    return {{"cancelled", true}};
  }
  if (command == "status") {
    std::lock_guard lock(s.mutex);
    const auto operation = request.at("operation_id").get<std::string>();
    if (operation == s.operation) return s.Snapshot(request.value("offset", size_t{}));
    const auto found = s.history.find(operation);
    if (found == s.history.end()) throw std::invalid_argument("FILE_REQUEST_NOT_FOUND");
    auto result = found->second;
    const auto &all = found->second.at("files");
    const size_t offset = request.value("offset", size_t{});
    if (offset > all.size()) throw std::invalid_argument("INVALID_ARGUMENT");
    result["files"] = json::array();
    for (size_t i = offset; i < std::min(offset + 256, all.size()); ++i) result["files"].push_back(all[i]);
    result["offset"] = offset;
    result["next_offset"] = offset + result["files"].size() < all.size() ? json(offset + result["files"].size()) : json(nullptr);
    return result;
  }
  if (command == "cancel") {
    std::vector<std::pair<uint32_t, uint64_t>> incoming;
    {
      std::lock_guard lock(s.mutex);
      if (request.at("operation_id") != s.operation) throw std::invalid_argument("FILE_REQUEST_NOT_FOUND");
      if (Terminal(s.state)) return s.Snapshot();
      for (auto &[id, file] : s.files) {
        if (Terminal(file.value("state", ""))) continue;
        file["state"] = "cancelled";
        if (file.value("direction", "") == "download") incoming.emplace_back(id, file.value("total", uint64_t{}));
      }
      s.uploads.clear(); s.state = "cancelled";
      s.output.clear(); s.output_bytes = 0;
      for (auto &[id, listing] : s.listings)
        if (listing.value("state", "") == "pending") listing = {{"state", "failed"}, {"error", "CANCELLED"}};
    }
    s.browser->Disconnect(s.operation);
    if (s.transfer->CancelSend()) {
      std::lock_guard lock(s.mutex);
      if (s.files.count(s.sending)) s.files[s.sending]["state"] = "cancelled";
    }
    for (const auto &[id, total] : incoming) {
      s.transfer->CancelReceive(id);
      s.browser->CancelTransfer(s.operation, id);
    }
    std::lock_guard lock(s.mutex);
    return s.Snapshot();
  }
  if (command != "upload" && command != "download") throw std::invalid_argument("INVALID_ARGUMENT");
  const auto operation = request.at("operation_id").get<std::string>();
  const auto paths = request.at("paths").get<std::vector<std::string>>();
  auto destination = request.at("destination").get<std::string>();
  if (paths.empty() || paths.size() > 256 || operation.empty() || destination.empty())
    throw std::invalid_argument("INVALID_ARGUMENT");
  if (command == "download") destination = Utf8(fs::canonical(fs::u8path(destination)));
  {
    std::lock_guard lock(s.mutex);
    if (s.arguments.count(operation)) {
      if (s.arguments.at(operation) != request.dump()) throw std::invalid_argument("OPERATION_ID_CONFLICT");
      return operation == s.operation ? s.Snapshot() : s.history.at(operation);
    }
    if (!s.operation.empty() && !Terminal(s.state)) throw std::invalid_argument("FILE_TRANSFER_BUSY");
    if (!s.operation.empty()) s.history[s.operation] = s.Snapshot(0, 4096);
    if (s.history.size() >= 4) {
      s.arguments.erase(s.history.begin()->first);
      s.history.erase(s.history.begin());
    }
    s.arguments[operation] = request.dump();
    s.operation = operation; s.state = "preparing"; s.prepared = false;
    s.destination = destination; s.download = command == "download";
    s.files.clear(); s.uploads.clear();
  }
  s.browser->Copy(operation, paths, destination, command == "download");
  return {{"operation_id", operation}, {"state", "preparing"}};
}

void ControllerFiles::Message(json message) {
  auto &s = *state_;
  std::string peer;
  {
    std::lock_guard lock(s.mutex);
    const auto op = message.value("op", json{});
    if (op == "reply") {
      if (!message.contains("id") || !message["id"].is_number_unsigned()) return;
      const auto found = s.routes.find(message["id"].get<uint32_t>());
      if (found == s.routes.end()) return;
      peer = found->second.peer; s.routes.erase(found);
    } else if (op == "prepare" || op == "notice") {
      if (Terminal(s.state) || message.value("transfer_id", json{}) != s.operation) return;
      if (op == "prepare" && (!s.download || message.value("destination", json{}) != s.destination)) return;
      peer = s.operation;
    } else return;
  }
  s.browser->Handle(peer, std::move(message));
}

void ControllerFiles::Receive(const char *data, size_t size, bool acknowledgement) {
  auto &s = *state_;
  if (acknowledgement) { s.transfer->Ack(data, size); return; }
  FileChunkView chunk;
  if (!DecodeFileChunk(data, size, &chunk)) return;
  {
    std::lock_guard lock(s.mutex);
    const auto found = s.files.find(chunk.header.file_id);
    if (found == s.files.end() || found->second.value("direction", "") != "download") return;
    if (Terminal(found->second.value("state", ""))) return;
  }
  s.transfer->Receive(data, size);
}

void ControllerFiles::Tick() {
  auto &s = *state_;
  auto events = s.browser->TakeEvents("remote");
  if (!s.operation.empty()) {
    auto transfers = s.browser->TakeEvents(s.operation);
    for (auto &event : transfers) events.push_back(std::move(event));
  }
  for (auto &event : events) {
    std::lock_guard lock(s.mutex);
    if (event.id) {
      if (s.listings.count(event.id)) { event.result["state"] = event.result.value("ok", json{}) == true ? "completed" : "failed"; s.listings[event.id] = std::move(event.result); }
    } else if (!s.operation.empty() && !Terminal(s.state)) {
      if (event.result.value("ok", json{}) == false) s.state = "failed";
      if (event.result.value("message", json{}) == "queued" &&
          (!s.download || event.result.value("op", json{}) == "notice")) s.prepared = true;
    }
  }
  std::optional<State::Upload> next;
  if (!s.transfer->Sending()) {
    std::lock_guard lock(s.mutex);
    if (!s.uploads.empty() && !Terminal(s.state)) {
      next = s.uploads.front(); s.uploads.pop_front(); s.sending = next->id;
    }
  }
  if (next && !s.transfer->Start(fs::u8path(next->path), Utf8(fs::u8path(next->path).filename()),
                                file_transfer::Session::Clock::now(), next->id)) {
    std::lock_guard lock(s.mutex); s.files[next->id]["state"] = "failed";
  }
  s.transfer->Pump();
  std::deque<std::pair<std::string, std::string>> output;
  {
    std::lock_guard lock(s.mutex); output.swap(s.output); s.output_bytes = 0;
    for (auto it = s.routes.begin(); it != s.routes.end();) {
      if (it->second.expires <= std::chrono::steady_clock::now()) it = s.routes.erase(it);
      else ++it;
    }
    if (s.prepared && !Terminal(s.state)) {
      bool complete = true, failed = false, cancelled = false;
      for (const auto &[id, file] : s.files) {
        const auto state = file.value("state", "queued");
        complete &= Terminal(state); failed |= state == "failed"; cancelled |= state == "cancelled";
      }
      s.state = complete ? (failed ? "failed" : cancelled ? "cancelled" : "completed") : "transferring";
    }
  }
  for (const auto &[stream, message] : output) {
    if (!s.send(stream.c_str(), message)) { std::lock_guard lock(s.mutex); s.state = "failed"; }
  }
}
} // namespace crossdesk
