#include "wayland_portal_shared.h"

#include <chrono>
#include <mutex>

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
#include <dbus/dbus.h>
#endif

#include "rd_log.h"

namespace crossdesk {

namespace {

std::mutex& SharedSessionMutex() {
  static std::mutex mutex;
  return mutex;
}

SharedWaylandPortalSessionInfo& SharedSessionInfo() {
  static SharedWaylandPortalSessionInfo info;
  return info;
}

bool& SharedSessionActive() {
  static bool active = false;
  return active;
}

int& SharedSessionRefs() {
  static int refs = 0;
  return refs;
}

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
constexpr const char* kPortalBusName = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalSessionInterface =
    "org.freedesktop.portal.Session";
constexpr int kPortalCloseWaitMs = 100;

void LogCloseDbusError(const char* action, DBusError* error) {
  if (error && dbus_error_is_set(error)) {
    LOG_ERROR("{} failed: {} ({})", action,
              error->message ? error->message : "unknown",
              error->name ? error->name : "unknown");
  } else {
    LOG_ERROR("{} failed", action);
  }
}

struct SessionClosedState {
  std::string session_handle;
  bool received = false;
};

DBusHandlerResult HandleSessionClosedSignal(DBusConnection* connection,
                                            DBusMessage* message,
                                            void* user_data) {
  (void)connection;
  auto* state = static_cast<SessionClosedState*>(user_data);
  if (!state || !message) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (!dbus_message_is_signal(message, kPortalSessionInterface, "Closed")) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  const char* path = dbus_message_get_path(message);
  if (!path || state->session_handle != path) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  state->received = true;
  return DBUS_HANDLER_RESULT_HANDLED;
}

bool BeginSessionClosedWatch(DBusConnection* connection,
                             const std::string& session_handle,
                             SessionClosedState* state,
                             std::string* match_rule_out) {
  if (!connection || session_handle.empty() || !state || !match_rule_out) {
    return false;
  }

  state->session_handle = session_handle;
  state->received = false;
  DBusError error;
  dbus_error_init(&error);
  const std::string match_rule =
      "type='signal',interface='" + std::string(kPortalSessionInterface) +
      "',member='Closed',path='" + session_handle + "'";
  dbus_bus_add_match(connection, match_rule.c_str(), &error);
  if (dbus_error_is_set(&error)) {
    LogCloseDbusError("dbus_bus_add_match(Session.Closed)", &error);
    dbus_error_free(&error);
    return false;
  }

  dbus_connection_add_filter(connection, HandleSessionClosedSignal, state,
                             nullptr);
  *match_rule_out = match_rule;
  return true;
}

void EndSessionClosedWatch(DBusConnection* connection, SessionClosedState* state,
                           const std::string& match_rule) {
  if (!connection || !state || match_rule.empty()) {
    return;
  }

  dbus_connection_remove_filter(connection, HandleSessionClosedSignal, state);

  DBusError remove_error;
  dbus_error_init(&remove_error);
  dbus_bus_remove_match(connection, match_rule.c_str(), &remove_error);
  if (dbus_error_is_set(&remove_error)) {
    dbus_error_free(&remove_error);
  }
}

void WaitForSessionClosed(DBusConnection* connection, SessionClosedState* state,
                          int timeout_ms = kPortalCloseWaitMs) {
  if (!connection || !state) {
    return;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!state->received && std::chrono::steady_clock::now() < deadline) {
    dbus_connection_read_write(connection, 100);
    while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS) {
    }
  }
}
#endif

}  // namespace

bool PublishSharedWaylandPortalSession(
    const SharedWaylandPortalSessionInfo& info) {
  if (!info.connection || info.session_handle.empty() || info.stream_id == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(SharedSessionMutex());
  if (SharedSessionActive()) {
    const auto& active_info = SharedSessionInfo();
    if (active_info.session_handle != info.session_handle &&
        SharedSessionRefs() > 0) {
      return false;
    }
  }

  const bool same_session =
      SharedSessionActive() &&
      SharedSessionInfo().session_handle == info.session_handle;
  SharedSessionInfo() = info;
  SharedSessionActive() = true;
  if (!same_session || SharedSessionRefs() <= 0) {
    SharedSessionRefs() = 1;
  }
  return true;
}

bool AcquireSharedWaylandPortalSession(bool require_pointer,
                                       SharedWaylandPortalSessionInfo* out) {
  if (!out) {
    return false;
  }

  std::lock_guard<std::mutex> lock(SharedSessionMutex());
  if (!SharedSessionActive()) {
    return false;
  }

  const auto& info = SharedSessionInfo();
  if (require_pointer && !info.pointer_granted) {
    return false;
  }

  ++SharedSessionRefs();
  *out = info;
  return true;
}

bool ReleaseSharedWaylandPortalSession(DBusConnection** connection_out,
                                       std::string* session_handle_out) {
  if (connection_out) {
    *connection_out = nullptr;
  }
  if (session_handle_out) {
    session_handle_out->clear();
  }

  std::lock_guard<std::mutex> lock(SharedSessionMutex());
  if (!SharedSessionActive()) {
    return false;
  }

  if (SharedSessionRefs() > 0) {
    --SharedSessionRefs();
  }

  if (SharedSessionRefs() > 0) {
    return true;
  }

  if (connection_out) {
    *connection_out = SharedSessionInfo().connection;
  }
  if (session_handle_out) {
    *session_handle_out = SharedSessionInfo().session_handle;
  }

  SharedSessionInfo() = SharedWaylandPortalSessionInfo{};
  SharedSessionActive() = false;
  SharedSessionRefs() = 0;
  return true;
}

void CloseWaylandPortalSessionAndConnection(DBusConnection* connection,
                                            const std::string& session_handle,
                                            const char* close_action) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  if (!connection) {
    return;
  }

  if (!session_handle.empty()) {
    SessionClosedState close_state;
    std::string close_match_rule;
    const bool watching_closed = BeginSessionClosedWatch(
        connection, session_handle, &close_state, &close_match_rule);

    DBusMessage* message = dbus_message_new_method_call(
        kPortalBusName, session_handle.c_str(), kPortalSessionInterface,
        "Close");
    if (message) {
      DBusError error;
      dbus_error_init(&error);
      DBusMessage* reply = dbus_connection_send_with_reply_and_block(
          connection, message, 1000, &error);
      if (!reply && dbus_error_is_set(&error)) {
        LogCloseDbusError(close_action, &error);
        dbus_error_free(&error);
      }
      if (reply) {
        dbus_message_unref(reply);
      }
      dbus_message_unref(message);
    }

    if (watching_closed) {
      WaitForSessionClosed(connection, &close_state);
      if (!close_state.received) {
        LOG_WARN("Timed out waiting for portal session to close: {}",
                 session_handle);
        LOG_WARN("Forcing local teardown without waiting for Session.Closed: {}",
                 session_handle);
        EndSessionClosedWatch(connection, &close_state, close_match_rule);
      } else {
        EndSessionClosedWatch(connection, &close_state, close_match_rule);
        LOG_INFO("Portal session closed: {}", session_handle);
      }
    }
  }

  dbus_connection_close(connection);
  dbus_connection_unref(connection);
#else
  (void)connection;
  (void)session_handle;
  (void)close_action;
#endif
}

}  // namespace crossdesk
