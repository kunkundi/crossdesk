#include "runtime/gui_runtime.h"

#include <SDL3/SDL_timer.h>

#include "rd_log.h"

#ifdef _WIN32
#include "interactive_desktop.h"
#include "platform/windows/screen_capturer/display_resolution_win.h"
#elif defined(__APPLE__)
#include "platform/macos/screen_capturer/display_resolution_mac.h"
#elif defined(__linux__) && !defined(__OHOS__)
#include "platform/linux/screen_capturer/display_resolution_linux.h"
#endif

namespace crossdesk {
namespace {
// Serialize native queries on transport threads with mode changes on the
// application thread (EnumDisplaySettings also caches its enumeration).
std::mutex native_display_resolution_mutex;
} // namespace

std::vector<DisplayResolutionInfo> GuiRuntime::QueryHostDisplayResolutions() {
#ifdef _WIN32
  std::lock_guard lock(native_display_resolution_mutex);
  return QueryWindowsDisplayResolutions(devices_.display_info_list());
#elif defined(__APPLE__)
  std::lock_guard lock(native_display_resolution_mutex);
  return QueryMacDisplayResolutions(devices_.display_info_list());
#elif defined(__linux__) && !defined(__OHOS__)
  std::lock_guard lock(native_display_resolution_mutex);
  return QueryLinuxDisplayResolutions(devices_.display_info_list());
#else
  return {};
#endif
}

void GuiRuntime::QueueDisplayResolutionChange(
    const std::string &id, PeerEventHandler *source,
    const DisplayResolutionChange &change) {
  const auto epoch = display_resolution_epoch_.load();
  const auto events = ControllerEvents(id);
  if (events.get() != source || !IsAuthorizedController(id))
    return;
  std::lock_guard lock(display_resolution_commands_mutex_);
  if (display_resolution_commands_.size() >= 32)
    return;
  display_resolution_commands_.push_back({id, events, change, epoch});
}

void GuiRuntime::BeginDisplayResolutionStartup(const std::string &id,
                                               PeerEventHandler *source) {
  const auto events = ControllerEvents(id);
  if (events.get() != source) return;
  std::lock_guard lock(display_resolution_commands_mutex_);
  auto &initial = initial_display_resolutions_[id];
  if (initial.source.lock() != events) initial = {};
  initial.source = events;
  if (!initial.state.connected)
    initial.generation = ++next_display_resolution_startup_;
  initial.state.Begin(SDL_GetTicks());
}

void GuiRuntime::ReceiveDisplayResolutionStartup(
    const std::string &id, PeerEventHandler *source,
    std::vector<DisplayResolutionPreference> preferences) {
  const auto events = ControllerEvents(id);
  if (events.get() != source) return;
  std::lock_guard lock(display_resolution_commands_mutex_);
  auto &initial = initial_display_resolutions_[id];
  if (initial.source.lock() != events) initial = {};
  initial.source = events;
  initial.state.Receive(std::move(preferences));
}

bool GuiRuntime::DisplayResolutionsReadyForCapture(const std::string &id) {
  std::lock_guard lock(display_resolution_commands_mutex_);
  const auto now = SDL_GetTicks();
  if (!id.empty()) {
    const auto found = initial_display_resolutions_.find(id);
    return found == initial_display_resolutions_.end() || found->second.state.Ready(now);
  }
  return std::all_of(initial_display_resolutions_.begin(),
      initial_display_resolutions_.end(), [&](const auto &entry) {
        return entry.second.state.Ready(now);
      });
}

bool GuiRuntime::ApplyHostDisplayResolution(const DisplayResolutionChange &change) {
  const auto available = QueryHostDisplayResolutions();
  const auto selected = std::find_if(available.begin(), available.end(),
      [&](const auto &info) { return SupportsDisplayResolution(info, change); });
  // Unsupported or stale requests must never create a restoration record.
  if (selected == available.end()) return false;
  if (selected->current == change.resolution) return true;
  const auto displays = devices_.display_info_list();
  std::lock_guard lock(native_display_resolution_mutex);
#ifdef _WIN32
  if (!IsCurrentSessionUserDesktopActive()) return false;
  return display_resolution_restoration_.Apply(change.display_key,
      [&] { return CaptureWindowsDisplayResolution(displays, change); },
      [&] { return ApplyWindowsDisplayResolution(displays, change); });
#elif defined(__APPLE__)
  return display_resolution_restoration_.Apply(change.display_key,
      [&] { return CaptureMacDisplayResolution(displays, change); },
      [&] { return ApplyMacDisplayResolution(displays, change); });
#elif defined(__linux__) && !defined(__OHOS__)
  return display_resolution_restoration_.Apply(change.display_key,
      [&] { return CaptureLinuxDisplayResolution(displays, change); },
      [&] { return ApplyLinuxDisplayResolution(displays, change); });
#else
  return false;
#endif
}

void GuiRuntime::RestoreHostDisplayResolutions(bool force) {
  const auto epoch = display_resolution_epoch_.load();
  const bool disconnected = epoch != handled_display_resolution_epoch_;
  handled_display_resolution_epoch_ = epoch;
  if (display_resolution_restoration_.Empty())
    return;
  const auto now = SDL_GetTicks();
  // A disconnect boundary must survive a rapid reconnect. Otherwise retry
  // failed restoration only while idle, without disrupting an active session.
  if (!disconnected && !force &&
      (now < next_display_resolution_restore_ ||
       !ConnectedControllers().empty()))
    return;
  next_display_resolution_restore_ = now + 2000;
  std::lock_guard lock(native_display_resolution_mutex);
#ifdef _WIN32
  if (!IsCurrentSessionUserDesktopActive())
    return;
#endif
  if (display_resolution_restoration_.RestoreAll())
    LOG_INFO("Restored original display modes after remote control");
  else if (disconnected || force)
    LOG_WARN("Could not restore every display mode; will retry while idle");
  next_display_resolution_poll_ = 0;
}

void GuiRuntime::HandleDisplayResolutionChanges() {
  RestoreHostDisplayResolutions();
  std::vector<std::pair<std::string, InitialDisplayResolutions>> initial;
  {
    std::lock_guard lock(display_resolution_commands_mutex_);
    for (auto &[id, startup] : initial_display_resolutions_) {
      if (!startup.state.NeedsApply()) continue;
      initial.emplace_back(id, startup);
    }
  }
  for (const auto &[id, startup] : initial) {
    const auto source = startup.source.lock();
    const auto active = [&] {
      if (stopping_ || !source || !source->IsActive() ||
          source != ControllerEvents(id) || !IsAuthorizedController(id))
        return false;
      std::lock_guard lock(display_resolution_commands_mutex_);
      const auto found = initial_display_resolutions_.find(id);
      return found != initial_display_resolutions_.end() &&
             found->second.generation == startup.generation &&
             found->second.source.lock() == source;
    };
    // Connected and metadata callbacks can interleave with this tick. Keep
    // early preferences pending until admission has actually been committed.
    if (!active()) continue;
    for (const auto &preference : startup.state.preferences) {
      RestoreHostDisplayResolutions();
      if (!active()) break;
      const auto available = QueryHostDisplayResolutions();
      const auto display = std::find_if(available.begin(), available.end(),
          [&](const auto &info) { return info.display_key == preference.display_key; });
      if (display == available.end()) continue;
      // Resolve a fresh slot from the native identity: hotplug can reorder the
      // list, and another display must never inherit the remembered mode.
      const DisplayResolutionChange change{display->display_id,
          display->display_key, preference.resolution};
      if (!ApplyHostDisplayResolution(change))
        LOG_INFO("Remembered display resolution is unavailable; keeping current mode");
    }
    RestoreHostDisplayResolutions();
    {
      std::lock_guard lock(display_resolution_commands_mutex_);
      const auto found = initial_display_resolutions_.find(id);
      if (found != initial_display_resolutions_.end() &&
          found->second.source.lock() == source &&
          found->second.generation == startup.generation) {
        found->second.state.applied = true;
        found->second.state.preferences.clear();
      }
    }
    if (active() && !startup.state.preferences.empty())
      AnnounceHostCapabilities(id);
  }
  std::deque<PendingDisplayResolution> commands;
  {
    std::lock_guard lock(display_resolution_commands_mutex_);
    commands.swap(display_resolution_commands_);
  }
  for (const auto &command : commands) {
    RestoreHostDisplayResolutions();
    const auto source = command.source.lock();
    if (stopping_ || command.epoch != display_resolution_epoch_.load() ||
        !source || !source->IsActive() ||
        source != ControllerEvents(command.remote_id) ||
        !IsAuthorizedController(command.remote_id))
      continue;
    RemoteAction reply;
    reply.type = ControlType::display_resolution_status;
    reply.resolution_change = command.change;
    reply.resolution_change.accepted = ApplyHostDisplayResolution(command.change);
    reply.display_resolutions = QueryHostDisplayResolutions();
    const auto message = reply.to_json();
    SendControllerData(command.remote_id, message.data(), message.size(),
                       control_data_label_.c_str());
    next_display_resolution_poll_ = 0;
  }
  // The transport can disconnect while a synchronous native mode switch is
  // in progress. Restore that change on this tick as well.
  RestoreHostDisplayResolutions();
  const auto now = SDL_GetTicks();
  if (now < next_display_resolution_poll_)
    return;
  next_display_resolution_poll_ = now + 2000;
  const auto controllers = ConnectedControllers();
  if (controllers.empty())
    return;
  auto current = QueryHostDisplayResolutions();
  if (current == last_display_resolutions_)
    return;
  last_display_resolutions_ = std::move(current);
  // Re-advertise after local changes/hotplug as well as remote requests. The
  // optional host-info extension is harmless to pre-feature controllers.
  const auto message = BuildHostInformation();
  for (const auto &id : controllers) {
    if (IsAuthorizedController(id))
      SendControllerData(id, message.data(), message.size(),
                         control_data_label_.c_str());
  }
}

} // namespace crossdesk
