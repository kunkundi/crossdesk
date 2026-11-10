#include "runtime/gui_runtime.h"

#include <SDL3/SDL_timer.h>

#ifdef _WIN32
#include "interactive_desktop.h"
#include "platform/windows/screen_capturer/display_resolution_win.h"
#endif

namespace crossdesk {
namespace {
// EnumDisplaySettings caches its enumeration. Serialize queries on transport
// threads with mode changes on the application thread.
std::mutex native_display_resolution_mutex;
} // namespace

std::vector<DisplayResolutionInfo> GuiRuntime::QueryHostDisplayResolutions() {
#ifdef _WIN32
  std::lock_guard lock(native_display_resolution_mutex);
  return QueryWindowsDisplayResolutions(devices_.display_info_list());
#else
  return {};
#endif
}

void GuiRuntime::QueueDisplayResolutionChange(
    const std::string &id, PeerEventHandler *source,
    const DisplayResolutionChange &change) {
  const auto events = ControllerEvents(id);
  if (events.get() != source || !IsAuthorizedController(id))
    return;
  std::lock_guard lock(display_resolution_commands_mutex_);
  if (display_resolution_commands_.size() >= 32)
    return;
  display_resolution_commands_.push_back({id, events, change});
}

void GuiRuntime::HandleDisplayResolutionChanges() {
  std::deque<PendingDisplayResolution> commands;
  {
    std::lock_guard lock(display_resolution_commands_mutex_);
    commands.swap(display_resolution_commands_);
  }
  for (const auto &command : commands) {
    const auto source = command.source.lock();
    if (!source || !source->IsActive() ||
        source != ControllerEvents(command.remote_id) ||
        !IsAuthorizedController(command.remote_id))
      continue;
    RemoteAction reply;
    reply.type = ControlType::display_resolution_status;
    reply.resolution_change = command.change;
    reply.resolution_change.accepted = false;
#ifdef _WIN32
    {
      std::lock_guard lock(native_display_resolution_mutex);
      if (IsCurrentSessionUserDesktopActive())
        reply.resolution_change.accepted = ApplyWindowsDisplayResolution(
            devices_.display_info_list(), command.change);
    }
#endif
    reply.display_resolutions = QueryHostDisplayResolutions();
    const auto message = reply.to_json();
    SendControllerData(command.remote_id, message.data(), message.size(),
                       control_data_label_.c_str());
    next_display_resolution_poll_ = 0;
  }
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
