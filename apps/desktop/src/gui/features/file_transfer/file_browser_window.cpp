#include "features/file_transfer/file_browser_window.h"

#include <algorithm>
#include <any>
#include <cctype>
#include <climits>
#include <cstdio>
extern unsigned char fa_solid_900_ttf[];
extern unsigned int fa_solid_900_ttf_len;
#include "ui/model_update.h"
#include "localization.h"
#include "application/window_geometry.h"
#include "application/window_presentation.h"
#if defined(__APPLE__)
#include "platform/window_drag.h"
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <SDL3/SDL.h>
#endif

namespace crossdesk {
namespace {
slint::SharedString S(std::string_view value) { return slint::SharedString(value); }
std::string Size(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = bytes; int unit = 0;
  while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
  char text[64]; std::snprintf(text, sizeof(text), unit ? "%.1f %s" : "%.0f %s", value, units[unit]);
  return text;
}
std::string Join(const std::string& parent, const std::string& name) {
  const bool windows = (parent.size() > 1 && parent[1] == ':') || parent.rfind("\\\\", 0) == 0;
  return parent + (parent.empty() || parent.back() == '/' || parent.back() == '\\'
      ? "" : windows ? "\\" : "/") + name;
}
std::string Name(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  return slash == std::string::npos || slash == path.size() - 1 ? path : path.substr(slash + 1);
}
bool Matches(const std::string& text, const std::string& filter) {
  return std::search(text.begin(), text.end(), filter.begin(), filter.end(),
      [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != text.end();
}
}
std::string FileBrowserWindow::Tr(const char* chinese, const char* english) const { return chinese_ ? chinese : english; }
FileBrowserWindow::FileBrowserWindow(FileTransferManager& transfers, std::string remote,
    bool controller, int language)
    : transfers_(transfers), remote_(std::move(remote)),
      connection_generation_(transfers_.browser().Generation(remote_)),
      chinese_(language == 0 || language == 3) {
  transfers_.browser().TakeEvents(remote_);
  window_->window().window_handle().register_font_from_data(
      fa_solid_900_ttf, fa_solid_900_ttf_len);
  // Resolve the native frame at its final size while it is still hidden.
  // Otherwise show() applies the preferred size after we have centered it.
  window_->window().set_size(slint::LogicalSize({900, 620}));
  window_->set_chinese(chinese_);
  UpdateActionLabels(language);
  SetPeerName("");
  window_->set_local_role(S(controller ? Tr("控制端", "Controller") : Tr("被控端", "Host")));
  window_->set_remote_role(S(controller ? Tr("被控端", "Host") : Tr("控制端", "Controller")));
  window_->set_local_entries(panes_[0].rows); window_->set_local_tree(panes_[0].tree);
  window_->set_remote_entries(panes_[1].rows); window_->set_remote_tree(panes_[1].tree);
  window_->set_tasks(tasks_);
  window_->on_navigate([this](bool remote, slint::SharedString path) { Navigate(remote, std::string(path)); });
  window_->on_refresh([this](bool remote) { Navigate(remote, panes_[remote].path); });
  window_->on_up([this](bool remote) { Navigate(remote, panes_[remote].parent); });
  window_->on_filter([this](bool remote, slint::SharedString filter) {
    auto& pane = panes_[remote]; pane.filter = std::string(filter); pane.selected.clear(); UpdatePane(remote);
  });
  window_->on_sort([this](bool remote, int column) {
    auto& pane = panes_[remote];
    if (column < 0 || column > 2 || pane.busy || !pane.loaded ||
        !pane.error.empty() || window_->get_drag_active()) return;
    const auto sort_column = static_cast<SortColumn>(column);
    pane.sort_descending = pane.sort_column == sort_column ? !pane.sort_descending
        : sort_column != SortColumn::Name;
    pane.sort_column = sort_column;
    SortEntries(pane);
    UpdatePane(remote);
  });
  window_->on_toggle_hidden([this](bool remote) {
    auto& pane = panes_[remote]; pane.show_hidden = !pane.show_hidden;
    pane.selected.clear(); UpdatePane(remote); UpdateTree(remote);
  });
  window_->on_select_entry([this](bool remote, int index) {
    auto& pane = panes_[remote];
    if (index < 0 || index >= static_cast<int>(pane.rows->row_count())) return;
    auto row = *pane.rows->row_data(index);
    if (row.link) return;
    SelectSource(remote);
    const std::string path(row.path);
    if (!pane.selected.erase(path)) pane.selected.insert(path);
    UpdatePane(remote);
  });
  window_->on_select_all([this](bool remote) {
    SelectSource(remote);
    auto& pane = panes_[remote];
    bool all = true;
    for (size_t i = 0; i < pane.rows->row_count(); ++i) {
      const auto row = *pane.rows->row_data(i);
      if (!row.link && !pane.selected.count(std::string(row.path))) all = false;
    }
    for (size_t i = 0; i < pane.rows->row_count(); ++i) {
      const auto row = *pane.rows->row_data(i);
      if (all) pane.selected.erase(std::string(row.path));
      else if (!row.link) pane.selected.insert(std::string(row.path));
    }
    UpdatePane(remote);
  });
  window_->on_clear_selection([this] {
    if (window_->get_drag_active()) return;
    for (int side = 0; side < 2; ++side) {
      panes_[side].selected.clear();
      UpdatePane(side);
    }
  });
  window_->on_open_entry([this](bool remote, int index) {
    auto& pane = panes_[remote];
    if (index < 0 || index >= static_cast<int>(pane.rows->row_count())) return;
    const auto row = *pane.rows->row_data(index);
    if (row.directory) OpenDirectory(remote, std::string(row.path));
  });
  window_->on_open_directory([this](bool remote, slint::SharedString path) {
    OpenDirectory(remote, std::string(path));
  });
  window_->on_tree_open([this](bool remote, int index) {
    const auto& pane = panes_[remote];
    if (index >= 0 && index < static_cast<int>(pane.tree->row_count()))
      Navigate(remote, std::string(pane.tree->row_data(index)->path));
  });
  window_->on_tree_toggle([this](bool remote, int index) {
    auto& pane = panes_[remote];
    if (index < 0 || index >= static_cast<int>(pane.tree->row_count())) return;
    const std::string path(pane.tree->row_data(index)->path);
    if (!pane.expanded.erase(path)) {
      pane.expanded.insert(path);
      if (!pane.directories.count(path)) Navigate(remote, path, true);
    }
    UpdateTree(remote);
  });
  window_->on_drag_data([this](bool remote, slint::SharedString path, int) {
    return MakeDragData(remote, std::string(path));
  });
  window_->on_drag_state([this](const slint::DataTransfer& data, bool active) {
    if (!active) { CancelDrag(); return; }
    const auto value = data.user_data();
    const auto stored = std::any_cast<std::shared_ptr<DragPayload>>(&value);
    if (!stored || !*stored) return;
    const auto payload = DropPayload(!(*stored)->remote, data);
    if (!payload) return;
    window_->set_drag_active(true);
    window_->set_drag_count(static_cast<int>(payload->paths->size()));
    SelectSource(payload->remote);
    panes_[payload->remote].selected = {payload->paths->begin(), payload->paths->end()};
    UpdatePane(payload->remote);
  });
  window_->on_can_drop([this](bool remote, const slint::DataTransfer& data) {
    return static_cast<bool>(DropPayload(remote, data));
  });
  window_->on_dropped([this](bool remote, const slint::DataTransfer& data) {
    const auto payload = DropPayload(remote, data);
    if (!payload) return false;
    // The drag owns a snapshot of both directories and its selected paths.
    // Consume before queueing so repeated release events cannot send twice.
    payload->consumed = true;
    return QueueTransfer(payload->remote, *payload->paths, payload->target_directory);
  });
  window_->on_cancel_drag([this] { CancelDrag(); });
  window_->on_transfer([this](bool download) { Transfer(download); });
  window_->on_cancel_task([this](int id) { transfers_.CancelBrowserTransfer(remote_, static_cast<uint32_t>(id)); });
  window_->on_clear_finished([this] {
    using Status = FileTransferManager::FileTransferState::FileTransferStatus;
    for (const auto& [id, task] : history_)
      if (task.status == Status::Completed || task.status == Status::Failed || task.status == Status::Cancelled)
        dismissed_.insert(id);
    next_sync_ = {};
  });
  window_->window().on_close_requested([this] {
    show_pending_ = false;
    CancelDrag();
    window_->hide(); return slint::CloseRequestResponse::KeepWindowShown;
  });
  Navigate(false, ""); Navigate(true, "");
}
void FileBrowserWindow::SetPeerName(const std::string& name) {
  const auto text = S(name.empty() ? Tr("远端设备", "Remote device") : name);
  if (window_->get_peer_name() != text) window_->set_peer_name(text);
}
void FileBrowserWindow::UpdateActionLabels(int language) {
  window_->set_send_local_text(S(localization::Literal("发送到本机", language)));
  window_->set_send_remote_text(S(localization::Literal("发送到远端", language)));
  window_->set_clear_completed_text(S(localization::Literal("清除已完成", language)));
  window_->set_open_directory_text(S(localization::Literal("打开目录", language)));
}
bool FileBrowserWindow::PresentsResult(uint32_t id,
    FileTransferManager::FileTransferState::FileTransferStatus status) const {
  using Status = FileTransferManager::FileTransferState::FileTransferStatus;
  if (!PresentsTransfers() ||
      (status != Status::Completed && status != Status::Failed && status != Status::Cancelled))
    return false;
  const auto task = history_.find(id);
  return task != history_.end() && !task->second.receiving && task->second.status == status &&
      (window_->get_tasks_expanded() || dismissed_.count(id));
}
bool FileBrowserWindow::CenterOnScreen() {
  auto& window = window_->window();
  if (window.is_fullscreen() || window.is_maximized()) return true;
#if defined(__APPLE__)
  return CenterNativeWindowOnScreen(window.appkit_view());
#elif defined(_WIN32)
  const HWND hwnd = window.win32_hwnd();
  if (!hwnd) return false;
  MONITORINFO monitor{sizeof(MONITORINFO)};
  RECT frame{};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) ||
      !GetWindowRect(hwnd, &frame)) return false;
  const auto& bounds = monitor.rcWork;
  const auto target = window_geometry::CenteredPosition(
      {static_cast<int>(bounds.left), static_cast<int>(bounds.top),
       static_cast<int>(bounds.right - bounds.left),
       static_cast<int>(bounds.bottom - bounds.top)},
      {static_cast<int>(frame.right - frame.left),
       static_cast<int>(frame.bottom - frame.top)});
  return SetWindowPos(hwnd, nullptr, target.x, target.y, 0, 0,
      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
#else
#if defined(__linux__) && !defined(__OHOS__)
  // Native Wayland placement belongs to the compositor.
  if (window.wayland_surface()) return true;
#endif
  const auto position = window.position();
  const auto size = window.size();
  if (!size.width || !size.height) return false;
  const SDL_Point center{position.x + static_cast<int>(size.width / 2),
                         position.y + static_cast<int>(size.height / 2)};
  SDL_DisplayID display = SDL_GetDisplayForPoint(&center);
  if (!display) display = SDL_GetPrimaryDisplay();
  SDL_Rect bounds{};
  if (!display || !SDL_GetDisplayUsableBounds(display, &bounds)) return false;
  const auto target = window_geometry::CenteredPosition(
      {bounds.x, bounds.y, bounds.w, bounds.h},
      {static_cast<int>(size.width), static_cast<int>(size.height)});
  window.set_position(slint::PhysicalPosition({target.x, target.y}));
  return true;
#endif
}
void FileBrowserWindow::Show() {
  if (!Tick()) return;
  if (Visible()) {
    window_->window().set_minimized(false);
  } else {
    show_pending_ = true;
    ShowWhenReady();
  }
  if (!panes_[1].busy && !panes_[1].error.empty()) Navigate(true, panes_[1].path);
}
void FileBrowserWindow::ShowWhenReady() {
  if (!show_pending_ || !NativeWindowReady(window_->window())) return;
#if defined(__APPLE__)
  if (!ConfigureWhiteWindowChrome(window_->window().appkit_view())) return;
#endif
  if (!CenterOnScreen()) return;
  show_pending_ = false;
  window_->show();
}
void FileBrowserWindow::OpenDirectory(bool remote, const std::string& path) {
  const auto& pane = panes_[remote];
  if (!SessionConnected() || !pane.loaded || pane.busy || !pane.error.empty() ||
      window_->get_drag_active()) return;
  // Resolve the menu target by path, never by a row index that can change.
  const auto entry = std::find_if(pane.entries.begin(), pane.entries.end(), [&](const auto& item) {
    return item.row.directory && std::string_view(item.row.path) == path;
  });
  if (entry != pane.entries.end()) Navigate(remote, path);
}
void FileBrowserWindow::Navigate(bool remote, const std::string& path, bool tree) {
  if (window_->get_drag_active()) CancelDrag();
  if (remote && !transfers_.BrowserConnected(remote_)) { Notice(Error("disconnected"), true); return; }
  auto& pane = panes_[remote];
  for (auto it = requests_.begin(); it != requests_.end();) {
    if (it->second.remote == remote && it->second.tree == tree &&
        (!tree || it->second.path == path)) {
      transfers_.browser().CancelList(remote_, it->first);
      it = requests_.erase(it);
    } else ++it;
  }
  const auto id = transfers_.browser().List(remote_, path, !remote);
  requests_[id] = {remote, tree, path};
  if (!tree) {
    pane.request = id; pane.busy = true; pane.error.clear(); pane.loading.clear();
    pane.selected.clear(); UpdatePane(remote);
  }
}
void FileBrowserWindow::SortEntries(Pane& pane) {
  std::sort(pane.entries.begin(), pane.entries.end(), [&](const Entry& a, const Entry& b) {
    // Folder grouping and unknown dates stay consistent in either direction.
    if (a.row.directory != b.row.directory) return a.row.directory;
    int order = 0;
    if (pane.sort_column == SortColumn::Modified) {
      const bool a_known = a.modified_ms.has_value() || !a.modified.empty();
      const bool b_known = b.modified_ms.has_value() || !b.modified.empty();
      if (a_known != b_known) return a_known;
      if (a.modified_ms && b.modified_ms)
        order = *a.modified_ms < *b.modified_ms ? -1 : *a.modified_ms > *b.modified_ms ? 1 : 0;
      else order = a.modified.compare(b.modified);
    } else if (pane.sort_column == SortColumn::Size && !a.row.directory) {
      order = a.bytes < b.bytes ? -1 : a.bytes > b.bytes ? 1 : 0;
    }
    if (!order) order = a.name_key.compare(b.name_key);
    if (!order) order = std::string_view(a.row.name).compare(std::string_view(b.row.name));
    if (!order) order = std::string_view(a.row.path).compare(std::string_view(b.row.path));
    return pane.sort_descending ? order > 0 : order < 0;
  });
}
void FileBrowserWindow::UpdatePane(bool remote) {
  auto& pane = panes_[remote];
  std::vector<ui::BrowserEntry> rows;
  size_t selectable = 0;
  rows.reserve(pane.entries.size());
  for (const auto& item : pane.entries) {
    auto entry = item.row;
    if (!pane.show_hidden && std::string(entry.name).starts_with('.')) continue;
    if (!Matches(std::string(entry.name), pane.filter)) continue;
    entry.selected = pane.selected.count(std::string(entry.path));
    if (!entry.link) ++selectable;
    rows.push_back(std::move(entry));
  }
  ui::UpdateModelRows(*pane.rows, std::move(rows));
  const auto summary = pane.busy
      ? pane.loading.empty() ? Tr("正在读取目录…", "Loading folder…") : pane.loading
      : std::to_string(pane.rows->row_count()) + Tr(" 项", " items");
  const bool up = pane.loaded && !pane.parent.empty() && pane.path != pane.parent;
  const bool all = selectable > 0 && pane.selected.size() == selectable;
  if (remote) {
    window_->set_remote_sort_column(static_cast<int>(pane.sort_column));
    window_->set_remote_sort_descending(pane.sort_descending);
    window_->set_remote_current_path(S(pane.path));
    window_->set_remote_filter(S(pane.filter));
    window_->set_remote_hidden(pane.show_hidden);
    window_->set_remote_busy(pane.busy); window_->set_remote_status(S(summary));
    window_->set_remote_error(S(pane.error)); window_->set_remote_up(up);
    window_->set_remote_selected(static_cast<int>(pane.selected.size())); window_->set_remote_all(all);
  } else {
    window_->set_local_sort_column(static_cast<int>(pane.sort_column));
    window_->set_local_sort_descending(pane.sort_descending);
    window_->set_local_current_path(S(pane.path));
    window_->set_local_filter(S(pane.filter));
    window_->set_local_hidden(pane.show_hidden);
    window_->set_local_busy(pane.busy); window_->set_local_status(S(summary));
    window_->set_local_error(S(pane.error)); window_->set_local_up(up);
    window_->set_local_selected(static_cast<int>(pane.selected.size())); window_->set_local_all(all);
  }
  UpdateAvailability();
}

void FileBrowserWindow::UpdateTree(bool remote) {
  auto& pane = panes_[remote];
  std::vector<ui::BrowserTreeEntry> rows;
  const auto append = [&](auto&& self, const std::string& path, const std::string& name, int depth) -> void {
    if (depth > 24) return;
    const bool expanded = pane.expanded.count(path);
    rows.push_back({S(name), S(path), depth, expanded, pane.path == path});
    if (expanded) {
      const auto it = pane.directories.find(path);
      if (it != pane.directories.end())
        for (const auto& entry : it->second)
          if (entry.directory && !entry.link && (pane.show_hidden || !std::string(entry.name).starts_with('.')))
            self(self, std::string(entry.path), std::string(entry.name), depth + 1);
    }
  };
  if (!pane.home.empty()) append(append, pane.home, Tr("主目录", "Home"), 0);
  append(append, "@roots", Tr("磁盘", "Drives"), 0);
  if (pane.loaded && pane.path != "@roots" && pane.path != pane.home &&
      std::none_of(rows.begin(), rows.end(), [&](const auto& row) { return std::string(row.path) == pane.path; }))
    append(append, pane.path, Name(pane.path), 0);
  ui::UpdateModelRows(*pane.tree, std::move(rows));
}
bool FileBrowserWindow::TransferReady() const {
  return window_->get_connected() && panes_[0].loaded && panes_[1].loaded &&
      !panes_[0].busy && !panes_[1].busy && panes_[0].error.empty() && panes_[1].error.empty() &&
      panes_[0].path != "@roots" && panes_[1].path != "@roots" &&
      std::string(window_->get_local_path()) == panes_[0].path &&
      std::string(window_->get_remote_path()) == panes_[1].path;
}
bool FileBrowserWindow::SessionConnected() const {
  // A peer can reconnect with the same ID between UI ticks. Its old window
  // must still close, even if the new connection is already usable.
  return !disconnected_ && transfers_.BrowserConnected(remote_) &&
      connection_generation_ == transfers_.browser().Generation(remote_);
}
void FileBrowserWindow::UpdateAvailability() {
  const bool ready = TransferReady();
  window_->set_transfer_ready(ready);
  window_->set_can_upload(ready && !panes_[0].selected.empty());
  window_->set_can_download(ready && !panes_[1].selected.empty());
  // All selected rows share one immutable list, so selecting a large folder
  // doesn't allocate a copy of the entire selection for every draggable row.
  for (int side = 0; side < 2; ++side)
    drag_selection_[side] = std::make_shared<const std::vector<std::string>>(
        panes_[side].selected.begin(), panes_[side].selected.end());
  // Callback bindings don't track C++ container mutations. Refresh the drag
  // payload only when its selection/directory state changes, never every tick.
  const int revision = window_->get_selection_revision();
  window_->set_selection_revision(revision == INT_MAX ? 0 : revision + 1);
}
void FileBrowserWindow::SelectSource(bool remote) {
  window_->set_source_remote(remote);
  if (!panes_[!remote].selected.empty()) {
    panes_[!remote].selected.clear(); UpdatePane(!remote);
  }
}
slint::DataTransfer FileBrowserWindow::MakeDragData(bool remote, const std::string& path) const {
  slint::DataTransfer result;
  if (!TransferReady()) return result;
  const auto& source = panes_[remote]; const auto& target = panes_[!remote];
  const auto found = std::find_if(source.entries.begin(), source.entries.end(),
      [&](const auto& entry) { return !entry.row.link && std::string(entry.row.path) == path; });
  if (found == source.entries.end()) return result;
  auto payload = std::make_shared<DragPayload>();
  payload->owner = drag_owner_; payload->epoch = drag_epoch_; payload->remote = remote;
  payload->source_request = source.request; payload->target_request = target.request;
  payload->source_directory = source.path; payload->target_directory = target.path;
  if (source.selected.count(path)) payload->paths = drag_selection_[remote];
  else payload->paths = std::make_shared<const std::vector<std::string>>(std::initializer_list<std::string>{path});
  // Opaque application data: a remote path must never masquerade as a local
  // file URL when the pointer leaves this window or enters another session.
  result.set_user_data(std::move(payload));
  return result;
}
std::shared_ptr<FileBrowserWindow::DragPayload> FileBrowserWindow::DropPayload(
    bool remote, const slint::DataTransfer& data) const {
  const auto value = data.user_data();
  const auto stored = std::any_cast<std::shared_ptr<DragPayload>>(&value);
  if (!stored || !*stored) return {};
  const auto& payload = *stored;
  if (payload->owner.lock() != drag_owner_ || payload->epoch != drag_epoch_ ||
      payload->consumed || payload->remote == remote || !payload->paths || payload->paths->empty() ||
      !TransferReady() || !SessionConnected()) return {};
  const auto& source = panes_[payload->remote]; const auto& target = panes_[remote];
  if (source.request != payload->source_request || target.request != payload->target_request ||
      source.path != payload->source_directory || target.path != payload->target_directory) return {};
  return payload;
}
void FileBrowserWindow::CancelDrag() {
  ++drag_epoch_;
  window_->set_drag_active(false); window_->set_drag_count(0);
  UpdateAvailability();
}
bool FileBrowserWindow::QueueTransfer(bool download, const std::vector<std::string>& paths,
                                      const std::string& destination) {
  if (paths.empty() || !TransferReady() || !SessionConnected()) return false;
  transfers_.browser().Copy(remote_, paths, destination, download);
  Notice(Tr("正在准备传输 → ", "Preparing transfer → ") + destination, false);
  panes_[download].selected.clear(); UpdatePane(download);
  return true;
}
void FileBrowserWindow::Transfer(bool download) {
  if (!TransferReady()) return;
  auto& source = panes_[download];
  const std::vector<std::string> paths(source.selected.begin(), source.selected.end());
  QueueTransfer(download, paths, panes_[!download].path);
}
void FileBrowserWindow::Notice(const std::string& message, bool error) {
  window_->set_notice(S(message)); window_->set_notice_error(error);
}
std::string FileBrowserWindow::Error(const std::string& code) const {
  if (code == "timeout") return Tr("远端未响应，请确认两端已更新至支持文件浏览的版本后重试。", "Peer did not respond. Update both ends to a version that supports file browsing, then retry.");
  if (code == "disconnected") return Tr("连接已断开，请重新连接后打开文件管理器。", "Disconnected. Reconnect and reopen the file manager.");
  if (code == "browser_not_open") return Tr("请先在控制端打开本连接的文件管理器，再浏览控制端目录。", "Open this connection's file manager on the controller to enable browsing its folders.");
  if (code == "directory_unavailable") return Tr("无法打开目录，请检查路径和访问权限后刷新。", "Cannot open folder. Check the path and access permissions, then refresh.");
  if (code == "pagination_unsupported") return Tr("远端版本无法完整读取此目录，请更新远端后重试。", "Update the remote app to load every item in this folder.");
  if (code == "invalid_listing" || code == "listing_expired") return Tr("目录列表未能完整加载，请刷新重试。", "The folder listing could not be fully loaded. Refresh to retry.");
  if (code == "directory_not_writable") return Tr("目标目录不可写，请选择其他目录。", "Cannot write to the destination. Choose another folder.");
  if (code == "symbolic_link") return Tr("所选内容包含符号链接，请选择实际文件或目录。", "Selection contains a symbolic link. Select the original files or folders.");
  if (code == "too_many_files") return Tr("单次最多传输 4096 个文件与目录，请分批选择。", "Select at most 4096 files and folders per transfer.");
  if (code == "file_unreadable") return Tr("文件无法读取，可能已移动或无访问权限。", "Cannot read a file. It may have moved or access was denied.");
  if (code == "busy") return Tr("请求较多，请稍后重试。", "Too many pending requests. Try again shortly.");
  if (code == "already_finished") return Tr("任务已结束，无需取消。", "This transfer has already finished.");
  return Tr("操作失败，请检查所选路径与文件权限后重试。", "Operation failed. Check the paths and file permissions, then retry.");
}
bool FileBrowserWindow::Tick() {
  using Status = FileTransferManager::FileTransferState::FileTransferStatus;
  // Connection lifetime is checked every UI tick, outside the throttled
  // directory/progress refresh. All native window operations stay on this thread.
  if (!SessionConnected()) {
    disconnected_ = true;
    show_pending_ = false;
    window_->set_connected(false);
    CancelDrag();
    requests_.clear();
    window_->hide();
    return false;
  }
  ShowWhenReady();
  const auto now = std::chrono::steady_clock::now();
  if (now < next_sync_) return true;
  next_sync_ = now + std::chrono::milliseconds(150);
  for (auto& event : transfers_.browser().TakeEvents(remote_)) {
    if (!event.id) {
      const bool ok = event.result.value("ok", false);
      if (!ok || !window_->get_notice_error())
        Notice(ok ? event.result.value("message", "") == "cancelling"
                    ? Tr("正在取消传输…", "Cancelling transfer…")
                    : Tr("已加入传输队列", "Added to transfer queue")
                  : Error(event.result.value("error", "unknown")), !ok);
      if (ok) refresh_at_ = now + std::chrono::seconds(1);
      continue;
    }
    const auto found = requests_.find(event.id);
    if (found == requests_.end()) continue;
    const auto request = found->second;
    auto& pane = panes_[request.remote];
    if (!request.tree && pane.request != event.id) continue;
    if (event.result.value("loading", false)) {
      if (!request.tree) {
        pane.loading = Tr("正在加载 ", "Loading ") + std::to_string(event.result.value("loaded", size_t{})) +
            " / " + std::to_string(event.result.value("total", size_t{})) + Tr(" 项…", " items…");
        if (request.remote) window_->set_remote_status(S(pane.loading));
        else window_->set_local_status(S(pane.loading));
      }
      continue;
    }
    requests_.erase(found);
    if (!request.tree) pane.busy = false;
    if (!event.result.value("ok", false)) {
      pane.error = Error(event.result.value("error", "unknown"));
      if (request.tree) pane.expanded.erase(request.path);
    } else {
      try {
        const auto path = event.result.at("path").get<std::string>();
        const auto& entries = event.result.at("entries");
        if (!entries.is_array()) throw std::runtime_error("invalid");
        std::vector<ui::BrowserEntry> rows;
        rows.reserve(entries.size());
        std::vector<Entry> sortable;
        if (!request.tree) sortable.reserve(entries.size());
        for (const auto& entry : entries) {
          ui::BrowserEntry row{};
          const auto name = entry.at("name").get<std::string>();
          if (name.empty() || name == "." || name == "..") continue;
          // Paths from a different operating system are opaque to this UI.
          row.name = S(name); row.path = S(path == "@roots" ? entry.at("path").get<std::string>() : Join(path, name));
          row.directory = entry.at("directory").get<bool>(); row.link = entry.value("link", false);
          const auto bytes = entry.at("size").get<uint64_t>();
          const auto timestamp = entry.at("modified").get<std::string>();
          row.size = S(Size(bytes));
          auto modified = timestamp.substr(0, 10);
          std::replace(modified.begin(), modified.end(), '-', '/');
          row.modified = S(modified);
          if (!request.tree) {
            Entry item{row, name, timestamp, bytes, std::nullopt};
            std::transform(item.name_key.begin(), item.name_key.end(), item.name_key.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (entry.contains("modified_ms") && entry["modified_ms"].is_number_integer())
              item.modified_ms = entry["modified_ms"].get<int64_t>();
            sortable.push_back(std::move(item));
          }
          rows.push_back(std::move(row));
        }
        pane.directories[path] = rows;
        if (!request.tree) {
          // A name filter belongs to the open folder. Preserve it on refresh,
          // but don't silently hide entries after navigating somewhere else.
          if (pane.path != path) pane.filter.clear();
          pane.path = path; pane.parent = event.result.at("parent").get<std::string>();
          if (request.path.empty()) pane.home = path;
          pane.entries = std::move(sortable); pane.error.clear(); pane.loaded = true;
          SortEntries(pane);
          pane.expanded.insert(path);
        }
      } catch (...) { pane.error = Error("unknown"); }
    }
    if (!request.tree) {
      if (request.remote) window_->set_remote_path(S(pane.path)); else window_->set_local_path(S(pane.path));
    }
    UpdatePane(request.remote); UpdateTree(request.remote);
  }
  bool new_tasks = false;
  for (const auto& task : transfers_.BrowserTransfers(remote_)) {
    new_tasks = new_tasks || (!task.receiving && !history_.count(task.file_id));
    history_[task.file_id] = task;
  }
  if (new_tasks) window_->set_tasks_expanded(true);
  std::vector<ui::BrowserTask> rows;
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    const auto& [id, task] = *it;
    if (task.status == Status::Completed && completed_.insert(id).second)
      refresh_at_ = now + std::chrono::milliseconds(500);
    // Received files still trigger directory refresh, but only this computer's
    // outgoing transfers appear in the task list and progress notifications.
    if (task.receiving || dismissed_.count(id)) continue;
    ui::BrowserTask row{};
    row.id = static_cast<int>(id); row.name = S(task.file_name); row.receiving = task.receiving;
    row.size = S(Size(task.file_size));
    row.active = task.status == Status::Queued || task.status == Status::Sending || task.status == Status::Receiving || task.status == Status::Cancelling;
    row.failed = task.status == Status::Failed;
    row.can_cancel = task.status == Status::Queued || task.status == Status::Sending || task.status == Status::Receiving;
    row.progress = task.status == Status::Completed ? 1.f : task.file_size ? std::clamp(float(task.sent_bytes) / task.file_size, 0.f, 1.f) : 0.f;
    const auto detail = Tr("已传输 ", "Transferred ") + Size(task.sent_bytes) +
        (task.rate_bps ? "  ·  " + Size(task.rate_bps / 8) + "/s" : "");
    row.detail = S(detail);
    switch (task.status) {
      case Status::Queued: row.status = S(Tr("排队中", "Queued")); break;
      case Status::Sending: row.status = S(Tr("发送中 ", "Sending ") + std::to_string(int(row.progress * 100)) + "%"); break;
      case Status::Receiving: row.status = S(Tr("接收中 ", "Receiving ") + std::to_string(int(row.progress * 100)) + "%"); break;
      case Status::Completed: row.status = S(Tr("已完成", "Completed")); break;
      case Status::Failed: row.status = S(Tr("失败 · 请重试", "Failed · Retry")); break;
      case Status::Cancelling: row.status = S(Tr("正在取消", "Cancelling")); break;
      case Status::Cancelled: row.status = S(Tr("已取消", "Cancelled")); break;
    }
    rows.push_back(row);
  }
  const auto active = std::count_if(rows.begin(), rows.end(), [](const auto& task) { return task.active; });
  const auto failed = std::count_if(rows.begin(), rows.end(), [](const auto& task) { return task.failed; });
  const auto summary = std::to_string(rows.size()) + Tr(" 项", " items") +
      (active ? " · " + std::to_string(active) + Tr(" 项进行中", " active") : "") +
      (failed ? " · " + std::to_string(failed) + Tr(" 项失败", " failed") : "");
  window_->set_task_summary(S(summary));
  ui::UpdateModelRows(*tasks_, std::move(rows));
  if (!window_->get_drag_active() && refresh_at_ != std::chrono::steady_clock::time_point{} && now >= refresh_at_) {
    refresh_at_ = {};
    for (int side = 0; side < 2; ++side)
      if (panes_[side].loaded && !panes_[side].busy && panes_[side].selected.empty() &&
          std::string(side ? window_->get_remote_path() : window_->get_local_path()) == panes_[side].path)
        Navigate(side, panes_[side].path);
  }
  const bool ready = TransferReady();
  if (window_->get_transfer_ready() != ready) UpdateAvailability();
  return true;
}

}  // namespace crossdesk
