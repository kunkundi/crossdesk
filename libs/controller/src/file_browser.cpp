#include "file_browser.h"

#include <file_transfer_core.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <set>
#include <stdexcept>

namespace crossdesk {
namespace {
using Json = FileBrowser::Json;
namespace fs = std::filesystem;
constexpr size_t kPageEntries = 512;
constexpr size_t kPageBytes = 45000;
std::string Utf8(const fs::path& path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}
fs::path Home() {
#ifdef _WIN32
  const auto* home = std::getenv("USERPROFILE");
#else
  const auto* home = std::getenv("HOME");
#endif
  return home ? std::filesystem::u8path(home) : fs::current_path();
}
Json Failure(const std::string& reason) { return {{"ok", false}, {"error", reason}}; }
template <typename FileTime>
auto SystemTime(FileTime time, FileTime file_now, std::chrono::system_clock::time_point system_now) {
  using Clock = typename FileTime::clock;
  if constexpr (requires { Clock::to_sys(time); })
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(Clock::to_sys(time));
  else
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(time - file_now + system_now);
}
std::pair<std::string, size_t> ParseCursor(const std::string& cursor) {
  const auto split = cursor.find(':');
  if (cursor.size() > 64 || split == std::string::npos || !split)
    throw std::runtime_error("invalid_listing");
  size_t offset = 0;
  const auto end = cursor.data() + cursor.size();
  const auto parsed = std::from_chars(cursor.data() + split + 1, end, offset);
  if (parsed.ec != std::errc{} || parsed.ptr != end)
    throw std::runtime_error("invalid_listing");
  return {cursor.substr(0, split), offset};
}
Json Page(const Json& listing, size_t offset, const std::string& snapshot) {
  const auto& entries = listing.at("entries");
  if (offset > entries.size()) throw std::runtime_error("invalid_listing");
  Json page{{"ok", true}, {"path", listing.at("path")}, {"parent", listing.at("parent")},
      {"entries", Json::array()}, {"offset", offset}, {"total", entries.size()}};
  size_t next = offset, bytes = 0;
  while (next < entries.size() && next - offset < kPageEntries) {
    const auto entry_bytes = entries[next].dump(-1, ' ', false, Json::error_handler_t::replace).size() + 1;
    if (entry_bytes > kPageBytes) throw std::runtime_error("invalid_listing");
    if (bytes + entry_bytes > kPageBytes) break;
    page["entries"].push_back(entries[next++]);
    bytes += entry_bytes;
  }
  page["truncated"] = next < entries.size();
  page["next"] = next < entries.size() && !snapshot.empty()
      ? snapshot + ":" + std::to_string(next) : "";
  return page;
}
std::string Text(const Json& value, const char* key) {
  const auto& field = value.at(key);
  if (!field.is_string()) throw std::runtime_error("invalid_request");
  auto text = field.get<std::string>();
  if (text.size() > 4096 || text.find('\0') != std::string::npos)
    throw std::runtime_error("invalid_path");
  return text;
}
fs::path ExistingDirectory(const std::string& path) {
  if (path.empty()) throw std::runtime_error("invalid_path");
  auto input = std::filesystem::u8path(path);
  if (!input.is_absolute()) throw std::runtime_error("invalid_path");
  std::error_code ec;
  auto result = fs::canonical(input, ec);
  if (ec || !fs::is_directory(result, ec)) throw std::runtime_error("directory_unavailable");
  return result;
}
fs::path Destination(const fs::path& root, const std::string& relative) {
  auto path = std::filesystem::u8path(relative);
  if (path.is_absolute() || path.has_root_name()) throw std::runtime_error("invalid_path");
  auto result = root;
  for (const auto& component : path) {
    if (component == "." || component == "..") throw std::runtime_error("invalid_path");
    result /= component;
    std::error_code ec;
    // Do not follow links within a transferred folder hierarchy.
    if (fs::is_symlink(fs::symlink_status(result, ec))) throw std::runtime_error("symbolic_link");
    ec.clear();
    fs::create_directory(result, ec);
    if (ec && ec != std::errc::file_exists) throw std::runtime_error("directory_not_writable");
    if (!fs::is_directory(result)) throw std::runtime_error("directory_not_writable");
  }
  return result;
}
}

FileBrowser::FileBrowser(Send send, Enqueue enqueue, Reserve reserve, Cancel cancel)
    : send_(std::move(send)), enqueue_(std::move(enqueue)), reserve_(std::move(reserve)), cancel_(std::move(cancel)),
      worker_([this] { Run(); }) {}
FileBrowser::~FileBrowser() { Stop(); }
void FileBrowser::Stop() {
  { std::lock_guard lock(mutex_); stopping_ = true; jobs_.clear();
    for (const auto& [_, listing] : listings_) listing->cancelled = true;
    listings_.clear();
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}
void FileBrowser::Post(const std::string& peer, std::function<void()> work) {
  std::lock_guard lock(mutex_);
  if (stopping_ || jobs_.size() >= 512) return;
  const auto generation = generations_[peer];
  jobs_.push_back([this, peer, generation, work = std::move(work)] {
    { std::lock_guard lock(mutex_);
      if (stopping_ || generations_[peer] != generation) return;
    }
    active_peer_ = peer; active_generation_ = generation;
    work();
  });
  wake_.notify_one();
}
void FileBrowser::Run() {
  for (;;) {
    std::function<void()> work;
    {
      std::unique_lock lock(mutex_);
      wake_.wait_for(lock, std::chrono::milliseconds(250), [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_) return;
      if (!jobs_.empty()) { work = std::move(jobs_.front()); jobs_.pop_front(); }
    }
    try { if (work) work(); } catch (...) { /* Invalid peer data cannot kill the worker. */ }
    std::erase_if(directory_snapshots_, [](const auto& entry) {
      return Clock::now() >= entry.second.expires;
    });
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (Clock::now() < it->second.deadline) { ++it; continue; }
      auto pending = std::move(it->second); it = pending_.erase(it);
      if (Current(pending.peer, pending.generation)) {
        active_peer_ = pending.peer; active_generation_ = pending.generation;
        pending.complete(Failure("timeout"));
      }
    }
  }
}
bool FileBrowser::Current(const std::string& peer, uint64_t generation) {
  std::lock_guard lock(mutex_);
  return !stopping_ && generations_[peer] == generation;
}
void FileBrowser::Report(const std::string& peer, Json result, bool notify_peer,
                         const std::string& transfer_id) {
  if (!transfer_id.empty()) result["transfer_id"] = transfer_id;
  if (!Current(peer, active_generation_)) return;
  Publish(peer, 0, result);
  if (notify_peer) {
    result["op"] = "notice"; result["id"] = next_id_++;
    SendMessage(peer, std::move(result));
  }
}
bool FileBrowser::SendMessage(const std::string& peer, Json message) {
  { std::lock_guard lock(mutex_);
    if (stopping_ || (peer == active_peer_ && generations_[peer] != active_generation_)) return false;
  }
  message["type"] = "file_browser"; message["version"] = 1;
  return send_(peer, message);
}
void FileBrowser::Publish(const std::string& peer, uint32_t id, Json result) {
  std::lock_guard lock(mutex_);
  auto& events = events_[peer];
  if (id) {
    for (auto& event : events) {
      if (event.id == id && event.result.value("loading", false)) {
        event.result = std::move(result);
        return;
      }
    }
  }
  if (events.size() < 512) events.push_back({id, std::move(result)});
}
std::vector<FileBrowser::Event> FileBrowser::TakeEvents(const std::string& peer) {
  std::lock_guard lock(mutex_);
  std::vector<Event> result; result.swap(events_[peer]); return result;
}
uint64_t FileBrowser::Generation(const std::string& peer) {
  std::lock_guard lock(mutex_);
  const auto found = generations_.find(peer);
  return found == generations_.end() ? 0 : found->second;
}
void FileBrowser::Disconnect(const std::string& peer) {
  {
    std::lock_guard lock(mutex_);
    ++generations_[peer]; events_.erase(peer);
    std::erase_if(listings_, [&](auto& entry) {
      if (entry.second->peer != peer) return false;
      entry.second->cancelled = true;
      return true;
    });
  }
  Post(peer, [this, peer] {
    std::erase_if(pending_, [&](const auto& entry) { return entry.second.peer == peer; });
    std::erase_if(directory_snapshots_, [&](const auto& entry) { return entry.first.first == peer; });
    Publish(peer, 0, Failure("disconnected"));
  });
}
void FileBrowser::Request(const std::string& peer, Json message,
                          std::function<void(const Json&)> complete) {
  if (pending_.size() >= 256) { complete(Failure("busy")); return; }
  const auto id = next_id_++;
  message["id"] = id;
  pending_.emplace(id, Pending{peer, active_generation_, Clock::now() + std::chrono::seconds(12), std::move(complete)});
  if (!SendMessage(peer, std::move(message))) {
    auto done = std::move(pending_.at(id).complete); pending_.erase(id);
    done(Failure("disconnected"));
  }
}
uint32_t FileBrowser::List(const std::string& peer, const std::string& path, bool local) {
  const auto id = next_id_++;
  auto listing = std::make_shared<Listing>();
  listing->id = id; listing->peer = peer; listing->path = path;
  {
    std::lock_guard lock(mutex_);
    listing->generation = generations_[peer];
    listings_[id] = listing;
  }
  Post(peer, [this, listing, local] {
    if (listing->cancelled) return;
    if (local) FinishList(listing, Directory(listing->path, [listing] { return listing->cancelled.load(); }));
    else ListPage(listing);
  });
  return id;
}
void FileBrowser::CancelList(const std::string& peer, uint32_t id) {
  std::lock_guard lock(mutex_);
  const auto found = listings_.find(id);
  if (found != listings_.end() && found->second->peer == peer) {
    found->second->cancelled = true;
    listings_.erase(found);
  }
  if (const auto events = events_.find(peer); events != events_.end())
    std::erase_if(events->second, [id](const auto& event) { return event.id == id; });
}
void FileBrowser::FinishList(const std::shared_ptr<Listing>& listing, Json result) {
  std::lock_guard lock(mutex_);
  if (listing->cancelled || stopping_ || generations_[listing->peer] != listing->generation) return;
  listings_.erase(listing->id);
  auto& events = events_[listing->peer];
  std::erase_if(events, [&](const auto& event) { return event.id == listing->id; });
  if (events.size() < 512) events.push_back({listing->id, std::move(result)});
}
void FileBrowser::ReleaseList(const std::string& peer, const std::string& cursor) {
  if (!cursor.empty()) Request(peer, {{"op", "list_cancel"}, {"cursor", cursor}}, [](const Json&) {});
}
void FileBrowser::ListPage(const std::shared_ptr<Listing>& listing) {
  if (listing->cancelled) { ReleaseList(listing->peer, listing->cursor); return; }
  Json request{{"op", "list"}, {"path", listing->path}, {"pagination", 1}};
  if (!listing->cursor.empty()) request["cursor"] = listing->cursor;
  Request(listing->peer, std::move(request), [this, listing](const Json& page) {
    std::string next;
    try {
      next = page.value("next", std::string{});
      if (listing->cancelled) { ReleaseList(listing->peer, next.empty() ? listing->cursor : next); return; }
      if (!page.value("ok", false)) {
        ReleaseList(listing->peer, listing->cursor);
        FinishList(listing, page); return;
      }
      const auto& entries = page.at("entries");
      if (!entries.is_array() || entries.size() > 2000 || next.size() > 64)
        throw std::runtime_error("invalid_listing");
      if ((page.contains("offset") && !page["offset"].is_number_unsigned()) ||
          (page.contains("total") && !page["total"].is_number_unsigned()) ||
          (!next.empty() && !page.value("truncated", false)))
        throw std::runtime_error("invalid_listing");
      if (page.value("truncated", false) && next.empty())
        throw std::runtime_error("pagination_unsupported");
      const bool first = listing->result.is_null();
      const size_t offset = first ? 0 : listing->result.at("entries").size();
      if (page.value("offset", offset) != offset ||
          (!first && (page.at("path") != listing->result.at("path") ||
                      page.at("parent") != listing->result.at("parent") ||
                      page.at("total") != listing->result.at("total"))))
        throw std::runtime_error("invalid_listing");
      const size_t total = page.value("total", entries.size());
      if (total < offset || entries.size() > total - offset ||
          (next.empty() ? offset + entries.size() != total : entries.empty() || offset + entries.size() >= total))
        throw std::runtime_error("invalid_listing");
      if (!next.empty() && ParseCursor(next).second != offset + entries.size())
        throw std::runtime_error("invalid_listing");
      if (first) {
        listing->result = {{"ok", true}, {"path", page.at("path")}, {"parent", page.at("parent")},
            {"entries", Json::array()}, {"total", total}, {"truncated", false}};
      }
      auto& all = listing->result["entries"].get_ref<Json::array_t&>();
      all.insert(all.end(), entries.begin(), entries.end());
      if (next.empty()) { FinishList(listing, std::move(listing->result)); return; }
      listing->cursor = next;
      Publish(listing->peer, listing->id, {{"ok", true}, {"loading", true},
          {"loaded", all.size()}, {"total", total}});
      // Yield between pages so newer navigation and transfer work can run.
      Post(listing->peer, [this, listing] { ListPage(listing); });
    } catch (const std::exception& error) {
      ReleaseList(listing->peer, next.empty() ? listing->cursor : next);
      FinishList(listing, Failure(std::string(error.what()) == "pagination_unsupported"
          ? "pagination_unsupported" : "invalid_listing"));
    }
  });
}
void FileBrowser::Copy(const std::string& peer, const std::vector<std::string>& paths,
                       const std::string& destination, bool download) {
  Post(peer, [this, peer, paths, destination, download] {
    if (download) Request(peer, {{"op", "copy"}, {"paths", paths}, {"destination", destination}},
        [this, peer](const Json& result) { Publish(peer, 0, result); });
    else Upload(peer, paths, destination);
  });
}
void FileBrowser::CancelTransfer(const std::string& peer, uint32_t id) {
  Post(peer, [this, peer, id] {
    Request(peer, {{"op", "cancel"}, {"file_id", id}}, [this, peer](const Json& result) {
      Publish(peer, 0, result);
    });
  });
}
void FileBrowser::Handle(const std::string& peer, Json message) {
  Post(peer, [this, peer, message = std::move(message)] {
    try {
      if (message.at("version") != 1 || !message.at("id").is_number_unsigned()) return;
      const auto wide_id = message.at("id").get<uint64_t>();
      if (!wide_id || wide_id > UINT32_MAX) return;
      const auto id = static_cast<uint32_t>(wide_id);
      if (message.value("op", "") == "notice") {
        if (message.at("ok").is_boolean()) Publish(peer, 0, message);
        return;
      }
      if (message.value("op", "") == "reply") {
        const auto it = pending_.find(id);
        if (it == pending_.end() || it->second.peer != peer || !message.at("ok").is_boolean()) return;
        const auto generation = it->second.generation;
        auto done = std::move(it->second.complete); pending_.erase(it);
        if (!Current(peer, generation)) return;
        active_generation_ = generation;
        done(message);
      } else Serve(peer, message);
    } catch (...) { }
  });
}

FileBrowser::Json FileBrowser::Directory(const std::string& value, const std::function<bool()>& cancelled) {
  try {
    Json entries = Json::array();
    if (value == "@roots") {
#ifdef _WIN32
      for (char drive = 'A'; drive <= 'Z'; ++drive) {
        const std::string root = std::string(1, drive) + ":\\";
        std::error_code ec;
        if (fs::is_directory(std::filesystem::u8path(root), ec))
          entries.push_back({{"name", root}, {"path", root}, {"directory", true}, {"size", 0}, {"modified", ""}, {"link", false}});
      }
#else
      entries.push_back({{"name", "/"}, {"path", "/"}, {"directory", true}, {"size", 0}, {"modified", ""}, {"link", false}});
#endif
      return {{"ok", true}, {"path", value}, {"parent", ""}, {"entries", entries}, {"truncated", false}};
    }
    auto path = ExistingDirectory(value.empty() ? Utf8(Home()) : value);
    std::error_code ec;
    fs::directory_iterator it(path, ec), end;
    if (ec) return Failure("directory_unavailable");
    // Reuse one clock offset for every entry so equal timestamps stay equal.
    const auto file_now = fs::file_time_type::clock::now();
    const auto system_now = std::chrono::system_clock::now();
    for (; it != end; it.increment(ec)) {
      if (cancelled && cancelled()) return Failure("cancelled");
      if (ec) return Failure("directory_unavailable");
      const auto name = Utf8(it->path().filename());
      if (name.rfind(".crossdesk-", 0) == 0) continue;
      std::error_code error;
      const auto status = it->symlink_status(error);
      if (error) continue;
      const bool link = fs::is_symlink(status);
      const bool directory = it->is_directory(error);
      if (!directory && !link && !fs::is_regular_file(status)) continue;
      uint64_t size = 0;
      if (!directory && !link) { size = it->file_size(error); if (error) size = 0; }
      std::string modified;
      Json modified_ms = nullptr;
      const auto time = it->last_write_time(error);
      if (!error) {
        const auto system_time = SystemTime(time, file_now, system_now);
        modified_ms = std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch()).count();
        const auto stamp = std::chrono::system_clock::to_time_t(system_time);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &stamp);
#else
        localtime_r(&stamp, &tm);
#endif
        char buffer[32]; std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        modified = buffer;
      }
      Json entry{{"name", name}, {"directory", directory}, {"size", size}, {"modified", modified},
          {"modified_ms", modified_ms}, {"link", link}};
      entries.push_back(std::move(entry));
    }
    if (ec) return Failure("directory_unavailable");
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      if (a["directory"] != b["directory"]) return a["directory"] == true;
      auto first = a["name"].template get<std::string>(), second = b["name"].template get<std::string>();
      const auto lower = [](unsigned char c) { return std::tolower(c); };
      auto folded_first = first, folded_second = second;
      std::transform(folded_first.begin(), folded_first.end(), folded_first.begin(), lower);
      std::transform(folded_second.begin(), folded_second.end(), folded_second.begin(), lower);
      return folded_first == folded_second ? first < second : folded_first < folded_second;
    });
    return {{"ok", true}, {"path", Utf8(path)}, {"parent", Utf8(path.parent_path())},
            {"entries", std::move(entries)}, {"truncated", false}};
  } catch (...) { return Failure("directory_unavailable"); }
}

FileBrowser::Json FileBrowser::DirectoryPage(const std::string& peer, const Json& message) {
  const auto now = Clock::now();
  const auto cursor = message.value("cursor", std::string{});
  if (!cursor.empty()) {
    const auto [token, offset] = ParseCursor(cursor);
    const auto found = directory_snapshots_.find({peer, token});
    if (found == directory_snapshots_.end() || found->second.generation != active_generation_ ||
        now >= found->second.expires) return Failure("listing_expired");
    auto page = Page(found->second.listing, offset, token);
    if (page.at("next") == "") directory_snapshots_.erase(found);
    else found->second.expires = now + std::chrono::seconds(30);
    return page;
  }
  const auto generation = active_generation_;
  auto listing = Directory(Text(message, "path"), [this, &peer, generation] { return !Current(peer, generation); });
  if (!listing.value("ok", false)) return listing;
  const bool paginated = message.value("pagination", Json{}) == 1;
  const auto token = paginated ? std::to_string(next_snapshot_++) : "";
  auto page = Page(listing, 0, token);
  if (paginated && page.at("next") != "") {
    const auto snapshots = std::count_if(directory_snapshots_.begin(), directory_snapshots_.end(),
        [&](const auto& entry) { return entry.first.first == peer; });
    if (snapshots >= 16) return Failure("busy");
    directory_snapshots_.emplace(std::make_pair(peer, token),
        DirectorySnapshot{std::move(listing), active_generation_, Clock::now() + std::chrono::seconds(30)});
  }
  return page;
}

void FileBrowser::Serve(const std::string& peer, const Json& message) {
  Json result;
  try {
    const auto op = Text(message, "op");
    if (op == "list") result = DirectoryPage(peer, message);
    else if (op == "list_cancel") {
      const auto [token, _] = ParseCursor(Text(message, "cursor"));
      directory_snapshots_.erase({peer, token});
      result = {{"ok", true}};
    }
    else if (op == "prepare") {
      auto root = ExistingDirectory(Text(message, "destination"));
      const auto& items = message.at("items");
      if (!items.is_array() || items.empty() || items.size() > 64) throw std::runtime_error("invalid_request");
      for (const auto& item : items) {
        if (!Current(peer, active_generation_)) return;
        if (!item.at("file_id").is_number_unsigned()) throw std::runtime_error("invalid_request");
        const auto id = item.at("file_id").get<uint64_t>();
        if (id > UINT32_MAX) throw std::runtime_error("invalid_request");
        auto dir = Destination(root, Text(item, "directory"));
        if (id && !reserve_(peer, static_cast<uint32_t>(id), dir)) throw std::runtime_error("directory_not_writable");
      }
      result = {{"ok", true}};
    } else if (op == "cancel") {
      const auto& id = message.at("file_id");
      if (!id.is_number_unsigned() || id.get<uint64_t>() > UINT32_MAX || !id.get<uint32_t>())
        throw std::runtime_error("invalid_request");
      if (!cancel_ || !cancel_(peer, id.get<uint32_t>())) throw std::runtime_error("already_finished");
      result = {{"ok", true}, {"message", "cancelling"}};
    } else if (op == "copy") {
      const auto& paths = message.at("paths");
      if (!paths.is_array() || paths.empty() || paths.size() > 256) throw std::runtime_error("invalid_request");
      std::vector<std::string> sources;
      for (const auto& path : paths) sources.push_back(Text(Json{{"path", path}}, "path"));
      Upload(peer, sources, Text(message, "destination"), true,
             message.contains("transfer_id") ? Text(message, "transfer_id") : "");
      result = {{"ok", true}, {"message", "queued"}};
    } else throw std::runtime_error("unsupported");
  } catch (const std::exception& error) { result = Failure(error.what()); }
  if (message.contains("transfer_id")) result["transfer_id"] = Text(message, "transfer_id");
  result["op"] = "reply"; result["id"] = message.at("id");
  SendMessage(peer, std::move(result));
}

void FileBrowser::Upload(const std::string& peer, const std::vector<std::string>& paths,
                         const std::string& destination, bool notify_peer, const std::string& transfer_id) {
  std::vector<UploadItem> files;
  try {
    if (paths.empty() || paths.size() > 256 || destination.empty()) throw std::runtime_error("invalid_request");
    std::set<fs::path> visited;
    const auto add = [&](const fs::path& path, const fs::path& relative) {
      if (!visited.insert(path).second) return;
      if (files.size() >= 4096) throw std::runtime_error("too_many_files");
      const auto status = fs::symlink_status(path);
      if (fs::is_symlink(status)) throw std::runtime_error("symbolic_link");
      if (fs::is_directory(status)) files.push_back({0, "", Utf8(relative)});
      else if (fs::is_regular_file(status)) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("file_unreadable");
        files.push_back({file_transfer::Session::NextFileId(), Utf8(path), Utf8(relative.parent_path())});
      } else throw std::runtime_error("unsupported_file");
    };
    // Validate the entire selection before creating remote folders or sending.
    for (const auto& source : paths) {
      auto path = std::filesystem::u8path(source);
      if (!path.is_absolute() || path.filename().empty()) throw std::runtime_error("invalid_path");
      add(path, path.filename());
      if (fs::is_directory(fs::symlink_status(path))) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
          { std::lock_guard lock(mutex_); if (stopping_ || generations_[peer] != active_generation_) return; }
          add(entry.path(), entry.path().lexically_relative(path.parent_path()));
        }
      }
    }
    auto job = std::make_shared<UploadJob>();
    job->files = std::move(files); job->destination = destination; job->notify_peer = notify_peer;
    job->transfer_id = transfer_id;
    UploadNext(peer, job);
  } catch (const std::exception& error) {
    Publish(peer, 0, Failure(error.what()));
    throw;
  }
}

void FileBrowser::UploadNext(const std::string& peer, const std::shared_ptr<UploadJob>& job) {
  if (!Current(peer, active_generation_)) return;
  if (job->next == job->files.size()) {
    Report(peer, {{"ok", true}, {"message", "queued"}}, job->notify_peer, job->transfer_id);
    return;
  }
  const auto start = job->next;
  Json items = Json::array();
  size_t bytes = job->destination.size();
  while (job->next < job->files.size() && items.size() < 32) {
    const auto& file = job->files[job->next];
    Json item{{"file_id", file.id}, {"directory", file.directory}};
    const auto size = item.dump().size();
    if (!items.empty() && bytes + size > 44000) break;
    bytes += size; items.push_back(std::move(item)); ++job->next;
  }
  const auto end = job->next;
  // Only one preparation batch per operation is in flight. Large folder
  // copies do not exhaust pending requests or reserve thousands of paths.
  Request(peer, {{"op", "prepare"}, {"destination", job->destination}, {"items", items},
                 {"transfer_id", job->transfer_id}},
      [this, peer, job, start, end](const Json& reply) {
        if (!reply.value("ok", false)) { Report(peer, reply, job->notify_peer, job->transfer_id); return; }
        for (auto index = start; index < end; ++index) {
          if (!Current(peer, active_generation_)) return;
          const auto& file = job->files[index];
          if (file.id && !enqueue_(peer, file.path, file.id)) {
            Report(peer, Failure("file_unreadable"), job->notify_peer, job->transfer_id); return;
          }
        }
        UploadNext(peer, job);
      });
}

}  // namespace crossdesk
