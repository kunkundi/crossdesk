#include "features/devices/screen_sharing_power.h"

#include "rd_log.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(__linux__) && !defined(__OHOS__)
#include <dbus/dbus.h>
#include <unistd.h>
#else
#include <SDL3/SDL.h>
#endif

namespace crossdesk {

#if defined(__linux__) && !defined(__OHOS__)
namespace {

constexpr const char* kApplication = "CrossDesk";
constexpr const char* kReason = "Sharing the desktop with a remote controller";
constexpr int kDbusTimeoutMs = 1000;

struct ConnectionCloser {
  void operator()(DBusConnection* connection) const {
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
  }
};
using BusConnection = std::unique_ptr<DBusConnection, ConnectionCloser>;
using BusMessage = std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)>;

BusConnection OpenBus(DBusBusType type) {
  DBusError error;
  dbus_error_init(&error);
  BusConnection connection(dbus_bus_get_private(type, &error));
  if (connection) {
    dbus_connection_set_exit_on_disconnect(connection.get(), false);
  } else {
    LOG_WARN("Cannot connect to power management bus: {}",
             error.message ? error.message : "unknown error");
  }
  dbus_error_free(&error);
  return connection;
}

BusMessage Call(DBusConnection* connection, DBusMessage* message) {
  return BusMessage(dbus_connection_send_with_reply_and_block(
                        connection, message, kDbusTimeoutMs, nullptr),
                    dbus_message_unref);
}

}  // namespace
#endif

struct ScreenSharingPower::Impl {
#ifdef _WIN32
  HANDLE request = INVALID_HANDLE_VALUE;
  bool display = false;
  bool system = false;

  void Start() {
    wchar_t reason_text[] = L"CrossDesk is sharing the desktop";
    REASON_CONTEXT reason{};
    reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    reason.Reason.SimpleReasonString = reason_text;
    request = PowerCreateRequest(&reason);
    if (request == INVALID_HANDLE_VALUE) {
      LOG_WARN("Cannot create screen sharing power request: {}", GetLastError());
      return;
    }
    // Handle-based requests also work in the unattended host, which has no
    // SDL video subsystem, and do not depend on the calling thread's lifetime.
    system = PowerSetRequest(request, PowerRequestSystemRequired) != 0;
    if (!system)
      LOG_WARN("Cannot prevent system idle sleep: {}", GetLastError());
    display = PowerSetRequest(request, PowerRequestDisplayRequired) != 0;
    if (!display)
      LOG_WARN("Cannot prevent display idle sleep: {}", GetLastError());
  }

  void Stop() {
    if (request == INVALID_HANDLE_VALUE) return;
    if (display) PowerClearRequest(request, PowerRequestDisplayRequired);
    if (system) PowerClearRequest(request, PowerRequestSystemRequired);
    CloseHandle(request);
    request = INVALID_HANDLE_VALUE;
    display = system = false;
  }
#elif defined(__APPLE__)
  IOPMAssertionID display = kIOPMNullAssertionID;
  IOPMAssertionID system = kIOPMNullAssertionID;
  IOPMAssertionID user_activity = kIOPMNullAssertionID;

  void Start() {
    const auto reason = CFSTR("CrossDesk is sharing the desktop");
    auto result = IOPMAssertionCreateWithName(
        kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
        reason, &system);
    if (result != kIOReturnSuccess)
      LOG_WARN("Cannot prevent system idle sleep: {}", result);
    result = IOPMAssertionCreateWithName(
        kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn,
        reason, &display);
    if (result != kIOReturnSuccess)
      LOG_WARN("Cannot prevent display idle sleep: {}", result);

    // The idle-sleep assertion cannot turn an already sleeping display on.
    // Wake it so ScreenCaptureKit can enumerate it on a locked reconnect.
    result = IOPMAssertionDeclareUserActivity(
        CFSTR("CrossDesk remote controller connected"), kIOPMUserActiveRemote,
        &user_activity);
    if (result != kIOReturnSuccess)
      LOG_WARN("Cannot wake the display for screen sharing: {}", result);
  }

  void Stop() {
    if (user_activity != kIOPMNullAssertionID)
      IOPMAssertionRelease(user_activity);
    if (display != kIOPMNullAssertionID) IOPMAssertionRelease(display);
    if (system != kIOPMNullAssertionID) IOPMAssertionRelease(system);
    user_activity = display = system = kIOPMNullAssertionID;
  }
#elif defined(__linux__) && !defined(__OHOS__)
  BusConnection session;
  dbus_uint32_t cookie = 0;
  bool desktop_inhibited = false;
  bool gnome = false;
  int sleep_fd = -1;

  bool InhibitDesktop(bool use_gnome) {
    const char* service = use_gnome ? "org.gnome.SessionManager"
                                    : "org.freedesktop.ScreenSaver";
    const char* path = use_gnome ? "/org/gnome/SessionManager"
                                : "/org/freedesktop/ScreenSaver";
    BusMessage message(dbus_message_new_method_call(service, path, service,
                                                    "Inhibit"),
                       dbus_message_unref);
    if (!message) return false;
    dbus_bool_t appended;
    if (use_gnome) {
      const dbus_uint32_t window = 0;
      const dbus_uint32_t flags = 4 | 8;  // Suspend and session idle.
      appended = dbus_message_append_args(
          message.get(), DBUS_TYPE_STRING, &kApplication, DBUS_TYPE_UINT32,
          &window, DBUS_TYPE_STRING, &kReason, DBUS_TYPE_UINT32, &flags,
          DBUS_TYPE_INVALID);
    } else {
      appended = dbus_message_append_args(
          message.get(), DBUS_TYPE_STRING, &kApplication, DBUS_TYPE_STRING,
          &kReason, DBUS_TYPE_INVALID);
    }
    if (!appended) return false;
    auto reply = Call(session.get(), message.get());
    if (!reply || !dbus_message_get_args(reply.get(), nullptr, DBUS_TYPE_UINT32,
                                        &cookie, DBUS_TYPE_INVALID))
      return false;
    gnome = use_gnome;
    desktop_inhibited = true;
    return true;
  }

  void Start() {
    if (!dbus_threads_init_default()) {
      LOG_WARN("Cannot initialize D-Bus for screen sharing power management");
      return;
    }
    // These session APIs work without an SDL window, including native Wayland
    // and a Slint window hidden in the tray. GNOME's API inhibits both kinds of
    // idle sleep; other desktops use ScreenSaver plus logind below.
    session = OpenBus(DBUS_BUS_SESSION);
    if (session && InhibitDesktop(true)) return;
    if (!session || !InhibitDesktop(false))
      LOG_WARN("Cannot inhibit desktop idle while sharing the screen");

    auto system_bus = OpenBus(DBUS_BUS_SYSTEM);
    if (!system_bus) return;
    BusMessage message(dbus_message_new_method_call(
                           "org.freedesktop.login1", "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager", "Inhibit"),
                       dbus_message_unref);
    const char* what = "sleep:idle";
    const char* mode = "block";
    if (!message || !dbus_message_append_args(
                        message.get(), DBUS_TYPE_STRING, &what,
                        DBUS_TYPE_STRING, &kApplication, DBUS_TYPE_STRING,
                        &kReason, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID)) {
      LOG_WARN("Cannot create system sleep inhibition request");
      return;
    }
    auto reply = Call(system_bus.get(), message.get());
    // get_args duplicates the returned fd with CLOEXEC. Holding this fd keeps
    // the inhibitor alive independently of the reply or D-Bus connection.
    if (!reply || !dbus_message_get_args(reply.get(), nullptr, DBUS_TYPE_UNIX_FD,
                                        &sleep_fd, DBUS_TYPE_INVALID))
      LOG_WARN("Cannot inhibit system sleep while sharing the screen");
  }

  void Stop() {
    if (sleep_fd >= 0) {
      close(sleep_fd);
      sleep_fd = -1;
    }
    if (session && desktop_inhibited &&
        dbus_connection_get_is_connected(session.get())) {
      const char* service = gnome ? "org.gnome.SessionManager"
                                  : "org.freedesktop.ScreenSaver";
      const char* path = gnome ? "/org/gnome/SessionManager"
                              : "/org/freedesktop/ScreenSaver";
      BusMessage message(dbus_message_new_method_call(
                             service, path, service,
                             gnome ? "Uninhibit" : "UnInhibit"),
                         dbus_message_unref);
      if (message && dbus_message_append_args(
                         message.get(), DBUS_TYPE_UINT32, &cookie,
                         DBUS_TYPE_INVALID)) {
        dbus_message_set_no_reply(message.get(), true);
        dbus_connection_send(session.get(), message.get(), nullptr);
        dbus_connection_flush(session.get());
      }
    }
    // A private connection also revokes the desktop inhibitor if UnInhibit
    // could not be sent. Nothing is retained by unrelated D-Bus clients.
    session.reset();
    desktop_inhibited = false;
    gnome = false;
    cookie = 0;
  }
#else
  void Start() {
    if (!SDL_DisableScreenSaver())
      LOG_WARN("Cannot prevent display idle sleep: {}", SDL_GetError());
  }
  void Stop() { SDL_EnableScreenSaver(); }
#endif
};

ScreenSharingPower::ScreenSharingPower() : impl_(std::make_unique<Impl>()) {}

ScreenSharingPower::~ScreenSharingPower() { SetSharing(false); }

void ScreenSharingPower::SetSharing(bool sharing) {
  if (sharing_ == sharing) return;
  sharing_ = sharing;
  if (sharing) {
    impl_->Start();
    LOG_INFO("Requested display and system idle-sleep inhibition for screen sharing");
  } else {
    impl_->Stop();
    LOG_INFO("Released screen sharing power inhibitors");
  }
}

}  // namespace crossdesk
