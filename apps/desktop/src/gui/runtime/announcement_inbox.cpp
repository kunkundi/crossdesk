#include "runtime/announcement_inbox.h"

#include <openssl/sha.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace crossdesk {
namespace {
using nlohmann::json;
bool Integer(const json& object, const char* key, int64_t low, int64_t high) {
  return object.contains(key) && object[key].is_number_integer() &&
         object[key] >= low && object[key] <= high;
}
std::string ScopeFilename(const std::string& scope) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(scope.data()), scope.size(),
         digest);
  std::string name;
  constexpr char hex[] = "0123456789abcdef";
  for (auto byte : digest) {
    name += hex[byte >> 4];
    name += hex[byte & 15];
  }
  return name + ".json";
}
}  // namespace

AnnouncementInbox::Snapshot AnnouncementInbox::Get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}
uint64_t AnnouncementInbox::Version() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.version;
}
void AnnouncementInbox::SetStorageDirectory(
    const std::filesystem::path& directory) {
  std::lock_guard<std::mutex> lock(mutex_);
  directory_ = directory;
}
void AnnouncementInbox::Configure(std::string host, int port,
                                  const std::string& device_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (directory_.empty() || host.empty() || port <= 0 || device_id.empty())
    return;
  for (auto& ch : host)
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
  if (host.back() == '.') host.pop_back();
  const auto scope = json::array({host, port, device_id}).dump();
  if (scope_ == scope) return;
  scope_ = scope;
  read_file_ = directory_ / ScopeFilename(scope);
  const auto version = state_.version + 1;
  state_ = {};
  state_.version = version;
  catalog_.clear();
  catalog_revision_ = -1;
  LoadReads();
  BeginCatalog();
}
void AnnouncementInbox::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto version = state_.version + 1;
  state_ = {};
  state_.version = version;
  scope_.clear();
  read_file_.clear();
  reads_.clear();
  catalog_.clear();
  catalog_revision_ = -1;
  connected_ = false;
  BeginCatalog();
}
void AnnouncementInbox::BeginCatalog() {
  pending_catalog_.clear();
  pending_revision_ = -1;
  pending_total_ = 0;
  phase_ = Phase::Catalog;
  request_id_.clear();
  state_.loading = false;
  refresh_ = connected_;
  ++state_.version;
}
void AnnouncementInbox::SetConnected(bool connected) {
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = connected;
  BeginCatalog();
}
void AnnouncementInbox::Refresh(int offset) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (offset >= 0) state_.offset = offset;
  if (offset < 0 || catalog_revision_ < 0) {
    BeginCatalog();
  } else {
    phase_ = Phase::Page;
    request_id_.clear();
    refresh_ = connected_;
  }
}
void AnnouncementInbox::UpdateReadingState() {
  state_.unread = 0;
  for (const auto& [id, revision] : catalog_) {
    const auto read = reads_.find(id);
    if (read == reads_.end() || read->second != revision) ++state_.unread;
  }
  for (auto& item : state_.items) {
    const auto read = reads_.find(item["id"]);
    item["read"] = read != reads_.end() && read->second == item["revision"];
  }
}
bool AnnouncementInbox::Read(int64_t id, int64_t revision) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& item : state_.items) {
    if (item["id"] != id || item["revision"] != revision) continue;
    if (item["read"].get<bool>()) return true;
    auto updated = reads_;
    updated[id] = revision;
    if (!SaveReads(updated)) {
      state_.read_save_failed = true;
      ++state_.version;
      return false;
    }
    reads_ = std::move(updated);
    state_.read_save_failed = false;
    UpdateReadingState();
    ++state_.version;
    return true;
  }
  return false;
}
void AnnouncementInbox::Fail() {
  request_id_.clear();
  refresh_ = false;
  state_.loading = false;
  state_.failed = true;
  ++state_.version;
}
nlohmann::json AnnouncementInbox::NextRequest(
    std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_ || scope_.empty()) return nullptr;
  if (state_.loading && now >= deadline_) Fail();
  if (!refresh_) return nullptr;
  refresh_ = false;
  request_id_ = std::to_string(++serial_);
  state_.loading = true;
  state_.failed = false;
  ++state_.version;
  deadline_ = now + std::chrono::seconds(12);
  return {{"type", "announcements_list"},
          {"request_id", request_id_},
          {"summary_only", phase_ == Phase::Catalog},
          {"offset", phase_ == Phase::Catalog
                         ? static_cast<int>(pending_catalog_.size())
                         : state_.offset}};
}
void AnnouncementInbox::Failed(const std::string& request_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (request_id == request_id_) Fail();
}
void AnnouncementInbox::Receive(const nlohmann::json& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!message.is_object() || !message.contains("request_id") ||
      message["request_id"] != request_id_ || request_id_.empty())
    return;
  if (message.contains("error")) {
    Fail();
    return;
  }
  const bool summary = phase_ == Phase::Catalog;
  const int limit = summary ? 200 : 20;
  if (!Integer(message, "offset", 0, INT32_MAX) ||
      !Integer(message, "total", 0, INT32_MAX) ||
      !Integer(message, "catalog_revision", 0, INT64_MAX) ||
      !message.contains("summary_only") || message["summary_only"] != summary ||
      !message.contains("items") || !message["items"].is_array() ||
      message["items"].size() > static_cast<size_t>(limit)) {
    Fail();
    return;
  }
  const int offset = message["offset"], total = message["total"];
  const int64_t revision = message["catalog_revision"];
  if (offset > total ||
      message["items"].size() !=
          static_cast<size_t>((std::min)(limit, total - offset))) {
    Fail();
    return;
  }
  const auto expected_revision =
      summary ? pending_revision_ : catalog_revision_;
  if (expected_revision >= 0 && expected_revision != revision) {
    BeginCatalog();
    return;
  }
  for (const auto& item : message["items"]) {
    if (!item.is_object() || !Integer(item, "id", 1, INT64_MAX) ||
        !Integer(item, "revision", 1, INT64_MAX)) {
      Fail();
      return;
    }
    if (!summary) {
      if (!Integer(item, "updated_at", 1, 32503680000LL)) {
        Fail();
        return;
      }
      for (const auto* key : {"title", "body"}) {
        if (!item.contains(key) || !item[key].is_string() ||
            item[key].get_ref<const std::string&>().size() >
                (std::string(key) == "title" ? 240u : 8000u)) {
          Fail();
          return;
        }
      }
      const auto found = catalog_.find(item["id"]);
      if (found == catalog_.end() || found->second != item["revision"]) {
        Fail();
        return;
      }
    }
  }
  request_id_.clear();
  state_.loading = false;
  if (summary) {
    if (pending_revision_ < 0) {
      pending_revision_ = revision;
      pending_total_ = total;
    }
    if (pending_total_ != total ||
        offset != static_cast<int>(pending_catalog_.size())) {
      Fail();
      return;
    }
    for (const auto& item : message["items"]) {
      if (!pending_catalog_.emplace(item["id"], item["revision"]).second) {
        Fail();
        return;
      }
    }
    if (static_cast<int>(pending_catalog_.size()) < total) {
      refresh_ = true;
      ++state_.version;
      return;
    }
    catalog_ = std::move(pending_catalog_);
    catalog_revision_ = revision;
    state_.total = total;
    auto retained = json::array();
    for (const auto& item : state_.items) {
      const auto found = catalog_.find(item["id"]);
      if (found != catalog_.end() && found->second == item["revision"])
        retained.push_back(item);
    }
    state_.items = std::move(retained);
    // Bound local history to published announcements; persist pruning on read.
    for (auto it = reads_.begin(); it != reads_.end();) {
      if (!catalog_.count(it->first))
        it = reads_.erase(it);
      else
        ++it;
    }
    phase_ = Phase::Page;
    refresh_ = total > 0;
    if (!total) {
      state_.offset = 0;
      state_.loaded = true;
    }
  } else {
    if (total != static_cast<int>(catalog_.size())) {
      Fail();
      return;
    }
    state_.items = message["items"];
    state_.offset = offset;
    state_.loaded = true;
  }
  UpdateReadingState();
  state_.failed = false;
  ++state_.version;
}

void AnnouncementInbox::LoadReads() {
  reads_.clear();
  std::error_code ec;
  const auto size = std::filesystem::file_size(read_file_, ec);
  if (ec || size > 16 * 1024 * 1024) return;
  std::ifstream input(read_file_, std::ios::binary);
  const auto data = json::parse(input, nullptr, false);
  if (!data.is_object() || !data.contains("scope") || data["scope"] != scope_ ||
      !data.contains("version") || data["version"] != 1 ||
      !data.contains("reads") || !data["reads"].is_object())
    return;
  for (const auto& entry : data["reads"].items()) {
    int64_t id = 0;
    const auto parsed = std::from_chars(
        entry.key().data(), entry.key().data() + entry.key().size(), id);
    if (parsed.ec == std::errc{} &&
        parsed.ptr == entry.key().data() + entry.key().size() && id > 0 &&
        entry.value().is_number_integer() && entry.value() > 0 &&
        entry.value() <= INT64_MAX)
      reads_[id] = entry.value();
  }
}
bool AnnouncementInbox::SaveReads(const Revisions& reads) const {
  if (read_file_.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(directory_, ec);
  if (ec) return false;
  auto entries = json::object();
  for (const auto& [id, revision] : reads)
    entries[std::to_string(id)] = revision;
  const auto payload =
      json({{"version", 1}, {"scope", scope_}, {"reads", entries}}).dump();
  auto temporary = read_file_;
  temporary += ".tmp-" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count());
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    output.close();
    if (!output) {
      std::filesystem::remove(temporary, ec);
      return false;
    }
  }
#ifdef _WIN32
  const bool saved =
      MoveFileExW(temporary.c_str(), read_file_.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::filesystem::rename(temporary, read_file_, ec);
  const bool saved = !ec;
#endif
  if (!saved) std::filesystem::remove(temporary, ec);
  return saved;
}
}  // namespace crossdesk
