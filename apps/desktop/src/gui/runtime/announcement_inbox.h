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
#include <vector>

namespace crossdesk {
// Announcement data is shared; read and dismissed versions stay on this device.
class AnnouncementInbox {
 public:
  struct Snapshot {
    nlohmann::json items = nlohmann::json::array();
    int total = 0, unread = 0;
    bool loaded = false, loading = false, failed = false;
    bool read_save_failed = false;
    bool delete_failed = false;
    uint64_t version = 0;
  };
  Snapshot Get() const;
  uint64_t Version() const;
  void SetStorageDirectory(const std::filesystem::path& directory);
  // Called on the UI thread after login supplies the actual device ID.
  void Configure(std::string host, int port, const std::string& device_id);
  void Reset();
  void SetConnected(bool connected);
  void Refresh();
  void LoadMore();
  bool Read(int64_t id, int64_t revision);
  bool Dismiss(int64_t id, int64_t revision);
  nlohmann::json TakePendingRequest(std::chrono::steady_clock::time_point now =
                                        std::chrono::steady_clock::now());
  void Failed(const std::string& request_id);
  void Receive(const nlohmann::json& message);

 private:
  using Revisions = std::map<int64_t, int64_t>;
  enum class Phase { Catalog, Page };
  void BeginCatalog();
  void Fail();
  void UpdateReadingState();
  bool IsDismissed(int64_t id, int64_t revision) const;
  void RebuildVisibleItems();
  void LoadLocalState();
  bool SaveLocalState(const Revisions& reads, const Revisions& dismissed) const;

  mutable std::mutex mutex_;
  Snapshot state_;
  std::filesystem::path directory_, read_file_;
  std::string scope_, request_id_;
  Revisions reads_, dismissed_, catalog_, pending_catalog_;
  // Keep bodies for the loaded list prefix. Lightweight page IDs let us skip
  // locally deleted entries without sending deletion state upstream.
  std::map<int, std::vector<int64_t>> page_ids_;
  std::map<int, nlohmann::json> page_bodies_;
  int pending_page_offset_ = 0;
  int visible_limit_ = 20;
  int64_t catalog_revision_ = -1, pending_revision_ = -1;
  int pending_total_ = 0;
  Phase phase_ = Phase::Catalog;
  bool connected_ = false, refresh_ = false;
  uint64_t serial_ = 0;
  std::chrono::steady_clock::time_point deadline_{};
};
}  // namespace crossdesk

#endif
