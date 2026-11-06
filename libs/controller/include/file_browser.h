/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FILE_BROWSER_H_
#define _FILE_BROWSER_H_

#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace crossdesk {

// Filesystem work is serialized off the UI and RTC threads. The adapter owns
// connection authorization and the existing reliable file transfer engine.
class FileBrowser {
 public:
  using Json = nlohmann::json;
  using Send = std::function<bool(const std::string&, const Json&)>;
  using Enqueue = std::function<bool(const std::string&, const std::string&, uint32_t)>;
  using Reserve = std::function<bool(const std::string&, uint32_t, const std::filesystem::path&)>;
  using Cancel = std::function<bool(const std::string&, uint32_t)>;
  struct Event { uint32_t id; Json result; };
  FileBrowser(Send send, Enqueue enqueue, Reserve reserve, Cancel cancel = {});
  ~FileBrowser();
  uint32_t List(const std::string& peer, const std::string& path, bool local);
  void CancelList(const std::string& peer, uint32_t id);
  void Copy(const std::string& peer, const std::vector<std::string>& paths,
            const std::string& destination, bool download);
  void CancelTransfer(const std::string& peer, uint32_t id);
  void Handle(const std::string& peer, Json message);
  void Disconnect(const std::string& peer);
  uint64_t Generation(const std::string& peer);
  std::vector<Event> TakeEvents(const std::string& peer);
  void Stop();
  static Json Directory(const std::string& path, const std::function<bool()>& cancelled = {});

 private:
  using Clock = std::chrono::steady_clock;
  struct Pending {
    std::string peer;
    uint64_t generation;
    Clock::time_point deadline;
    std::function<void(const Json&)> complete;
  };
  struct Listing {
    uint32_t id;
    uint64_t generation;
    std::string peer, path, cursor;
    Json result;
    std::atomic<bool> cancelled{false};
  };
  struct DirectorySnapshot {
    Json listing;
    uint64_t generation;
    Clock::time_point expires;
  };
  struct UploadItem { uint32_t id; std::string path, directory; };
  struct UploadJob {
    std::vector<UploadItem> files;
    size_t next = 0;
    std::string destination;
    bool notify_peer = false;
    std::string transfer_id;
  };
  void Post(const std::string& peer, std::function<void()> work);
  void Run();
  void Request(const std::string& peer, Json message,
               std::function<void(const Json&)> complete);
  void Publish(const std::string& peer, uint32_t id, Json result);
  void Serve(const std::string& peer, const Json& message);
  void ListPage(const std::shared_ptr<Listing>& listing);
  void FinishList(const std::shared_ptr<Listing>& listing, Json result);
  void ReleaseList(const std::string& peer, const std::string& cursor);
  Json DirectoryPage(const std::string& peer, const Json& message);
  void Upload(const std::string& peer, const std::vector<std::string>& paths,
              const std::string& destination, bool notify_peer = false,
              const std::string& transfer_id = {});
  void UploadNext(const std::string& peer, const std::shared_ptr<UploadJob>& job);
  bool Current(const std::string& peer, uint64_t generation);
  void Report(const std::string& peer, Json result, bool notify_peer,
              const std::string& transfer_id = {});
  bool SendMessage(const std::string& peer, Json message);
  Send send_;
  Enqueue enqueue_;
  Reserve reserve_;
  Cancel cancel_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::deque<std::function<void()>> jobs_;
  std::map<std::string, uint64_t> generations_;
  std::map<std::string, std::vector<Event>> events_;
  std::map<uint32_t, std::shared_ptr<Listing>> listings_;
  // Worker-thread state only.
  std::map<uint32_t, Pending> pending_;
  std::map<std::pair<std::string, std::string>, DirectorySnapshot> directory_snapshots_;
  uint64_t next_snapshot_ = 1;
  std::string active_peer_;
  uint64_t active_generation_ = 0;
  std::atomic<uint32_t> next_id_{1};
  std::thread worker_;
};

}  // namespace crossdesk

#endif
