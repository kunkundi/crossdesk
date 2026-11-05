/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _FILE_BROWSER_WINDOW_H_
#define _FILE_BROWSER_WINDOW_H_

#include "crossdesk_ui.h"
#include "features/file_transfer/file_transfer_manager.h"
#include <optional>
#include <set>

namespace crossdesk {

class FileBrowserWindow {
 public:
  FileBrowserWindow(FileTransferManager& transfers, std::string remote,
                    bool controller, int language);
  void Show();
  void SetPeerName(const std::string& name);
  void UpdateActionLabels(int language);
  // False means this connection ended; the UI owner should discard the window.
  bool Tick();
  bool Visible() const { return window_->window().is_visible(); }
  bool PresentsTransfers() const { return Visible() && !window_->window().is_minimized(); }
  bool PresentsResult(uint32_t id, FileTransferManager::FileTransferState::FileTransferStatus status) const;

 private:
  enum class SortColumn { Name, Modified, Size };
  struct Entry {
    ui::BrowserEntry row;
    std::string name_key, modified;
    uint64_t bytes = 0;
    std::optional<int64_t> modified_ms;
  };
  struct Pane {
    std::string path, parent, home, filter, error, loading;
    bool loaded = false, busy = false, show_hidden = false;
    uint32_t request = 0;
    SortColumn sort_column = SortColumn::Name;
    bool sort_descending = false;
    std::vector<Entry> entries;
    std::set<std::string> selected, expanded;
    std::map<std::string, std::vector<ui::BrowserEntry>> directories;
    std::shared_ptr<slint::VectorModel<ui::BrowserEntry>> rows =
        std::make_shared<slint::VectorModel<ui::BrowserEntry>>();
    std::shared_ptr<slint::VectorModel<ui::BrowserTreeEntry>> tree =
        std::make_shared<slint::VectorModel<ui::BrowserTreeEntry>>();
  };
  struct Request { bool remote, tree; std::string path; };
  struct DragPayload {
    std::weak_ptr<int> owner;
    uint64_t epoch = 0;
    bool remote = false, consumed = false;
    uint32_t source_request = 0, target_request = 0;
    std::string source_directory, target_directory;
    std::shared_ptr<const std::vector<std::string>> paths;
  };
  void Navigate(bool remote, const std::string& path, bool tree = false);
  void OpenDirectory(bool remote, const std::string& path);
  bool CenterOnScreen();
  void ShowWhenReady();
  void UpdatePane(bool remote);
  void SortEntries(Pane& pane);
  void UpdateTree(bool remote);
  bool TransferReady() const;
  bool SessionConnected() const;
  void UpdateAvailability();
  void SelectSource(bool remote);
  slint::DataTransfer MakeDragData(bool remote, const std::string& path) const;
  std::shared_ptr<DragPayload> DropPayload(bool remote, const slint::DataTransfer& data) const;
  void CancelDrag();
  bool QueueTransfer(bool download, const std::vector<std::string>& paths,
                     const std::string& destination);
  void Transfer(bool download);
  void Notice(const std::string& message, bool error);
  std::string Error(const std::string& code) const;
  std::string Tr(const char* chinese, const char* english) const;
  FileTransferManager& transfers_;
  std::string remote_;
  uint64_t connection_generation_;
  bool chinese_;
  bool disconnected_ = false;
  bool show_pending_ = false;
  slint::ComponentHandle<ui::FileBrowserWindow> window_ = ui::FileBrowserWindow::create();
  Pane panes_[2];
  std::shared_ptr<int> drag_owner_ = std::make_shared<int>(0);
  std::shared_ptr<const std::vector<std::string>> drag_selection_[2];
  uint64_t drag_epoch_ = 0;
  std::map<uint32_t, Request> requests_;
  std::set<uint32_t> dismissed_, completed_;
  std::map<uint32_t, FileTransferManager::FileTransferState::FileTransferInfo> history_;
  std::shared_ptr<slint::VectorModel<ui::BrowserTask>> tasks_ =
      std::make_shared<slint::VectorModel<ui::BrowserTask>>();
  std::chrono::steady_clock::time_point next_sync_{}, refresh_at_{};
};

}  // namespace crossdesk

#endif
