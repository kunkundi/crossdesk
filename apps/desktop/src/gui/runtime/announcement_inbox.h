/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-29
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ANNOUNCEMENT_INBOX_H_
#define _ANNOUNCEMENT_INBOX_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

namespace crossdesk {
// Announcement data is shared; reading state only persists to the local file.
class AnnouncementInbox {
 public:
  struct Snapshot {
    nlohmann::json items = nlohmann::json::array();
    int offset = 0, total = 0, unread = 0;
    bool loaded = false, loading = false, failed = false;
    bool read_save_failed = false;
    uint64_t version = 0;
  };
  Snapshot Get() const;
  uint64_t Version() const;
  void SetStorageDirectory(const std::filesystem::path& directory);
  // Called on the UI thread after login supplies the actual device ID.
  void Configure(std::string host, int port, const std::string& device_id);
  void Reset();
  void SetConnected(bool connected);
  void Refresh(int offset = -1);
  bool Read(int64_t id, int64_t revision);
  nlohmann::json NextRequest(std::chrono::steady_clock::time_point now =
                                 std::chrono::steady_clock::now());
  void Failed(const std::string& request_id);
  void Receive(const nlohmann::json& message);

 private:
  using Revisions = std::map<int64_t, int64_t>;
  enum class Phase { Catalog, Page };
  void BeginCatalog();
  void Fail();
  void UpdateReadingState();
  void LoadReads();
  bool SaveReads(const Revisions& reads) const;

  mutable std::mutex mutex_;
  Snapshot state_;
  std::filesystem::path directory_, read_file_;
  std::string scope_, request_id_;
  Revisions reads_, catalog_, pending_catalog_;
  int64_t catalog_revision_ = -1, pending_revision_ = -1;
  int pending_total_ = 0;
  Phase phase_ = Phase::Catalog;
  bool connected_ = false, refresh_ = false;
  uint64_t serial_ = 0;
  std::chrono::steady_clock::time_point deadline_{};
};
}  // namespace crossdesk

#endif