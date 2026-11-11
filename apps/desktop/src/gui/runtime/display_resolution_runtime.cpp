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
    reply.resolution_change.accepted = false;
    const auto displays = devices_.display_info_list();
    const auto available = QueryHostDisplayResolutions();
    const auto selected =
        std::find_if(available.begin(), available.end(), [&](const auto &info) {
          return SupportsDisplayResolution(info, command.change);
        });
    if (selected == available.end()) {
      // Unsupported or stale requests must never create a restoration record.
    } else if (selected->current == command.change.resolution) {
      reply.resolution_change.accepted = true;
    } else {
#ifdef _WIN32
      {
        std::lock_guard lock(native_display_resolution_mutex);
        if (IsCurrentSessionUserDesktopActive())
          reply.resolution_change.accepted =
              display_resolution_restoration_.Apply(
                  command.change.display_key,
                  [&] {
                    return CaptureWindowsDisplayResolution(displays,
                                                           command.change);
                  },
                  [&] {
                    return ApplyWindowsDisplayResolution(displays,
                                                         command.change);
                  });
      }
#elif defined(__APPLE__)
      {
        std::lock_guard lock(native_display_resolution_mutex);
        reply.resolution_change.accepted =
            display_resolution_restoration_.Apply(
                command.change.display_key,
                [&] {
                  return CaptureMacDisplayResolution(displays, command.change);
                },
                [&] {
                  return ApplyMacDisplayResolution(displays, command.change);
                });
      }
#elif defined(__linux__) && !defined(__OHOS__)
      {
        std::lock_guard lock(native_display_resolution_mutex);
        reply.resolution_change.accepted =
            display_resolution_restoration_.Apply(
                command.change.display_key,
                [&] {
                  return CaptureLinuxDisplayResolution(displays, command.change);
                },
                [&] {
                  return ApplyLinuxDisplayResolution(displays, command.change);
                });
      }
#endif
    }
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
