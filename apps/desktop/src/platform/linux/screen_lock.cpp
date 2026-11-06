/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#include "screen_lock.h"

#include <dbus/dbus.h>
#include <unistd.h>

#include "rd_log.h"

namespace crossdesk {

ScreenLockState QueryScreenLock() {
  dbus_threads_init_default();
  DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SYSTEM, nullptr);
  if (!connection) return ScreenLockState::unknown;
  dbus_connection_set_exit_on_disconnect(connection, false);
  ScreenLockState state = ScreenLockState::unknown;
  DBusMessage* query = dbus_message_new_method_call("org.freedesktop.login1",
      "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "GetSessionByPID");
  if (query) {
    dbus_uint32_t pid = getpid();
    dbus_message_append_args(query, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID);
    DBusMessage* session = dbus_connection_send_with_reply_and_block(connection, query, 500, nullptr);
    dbus_message_unref(query);
    const char* path = nullptr;
    if (session && dbus_message_get_args(session, nullptr, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID)) {
      query = dbus_message_new_method_call("org.freedesktop.login1", path,
          "org.freedesktop.DBus.Properties", "Get");
      const char* interface = "org.freedesktop.login1.Session";
      const char* property = "LockedHint";
      if (query) {
        dbus_message_append_args(query, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, query, 500, nullptr);
        dbus_message_unref(query);
        DBusMessageIter outer, value;
        if (reply && dbus_message_iter_init(reply, &outer) && dbus_message_iter_get_arg_type(&outer) == DBUS_TYPE_VARIANT) {
          dbus_message_iter_recurse(&outer, &value);
          if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_BOOLEAN) {
            dbus_bool_t locked = false;
            dbus_message_iter_get_basic(&value, &locked);
            state = locked ? ScreenLockState::locked : ScreenLockState::unlocked;
          }
        }
        if (reply) dbus_message_unref(reply);
      }
    }
    if (session) dbus_message_unref(session);
  }
  dbus_connection_close(connection);
  dbus_connection_unref(connection);
  return state;
}

bool LockScreen() {
  dbus_threads_init_default();
  DBusError error;
  dbus_error_init(&error);
  DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
  if (!connection) {
    LOG_WARN("Cannot connect to screen lock service: {}",
             error.message ? error.message : "session bus unavailable");
    dbus_error_free(&error);
    return false;
  }
  dbus_connection_set_exit_on_disconnect(connection, false);

  // Address the current desktop's session bus, including headless desktops;
  // never lock every logged-in user or an unrelated system login session.
  struct Service { const char* name; const char* path; };
  constexpr Service services[] = {
      {"org.freedesktop.ScreenSaver", "/ScreenSaver"},
      {"org.gnome.ScreenSaver", "/org/gnome/ScreenSaver"},
      {"org.cinnamon.ScreenSaver", "/org/cinnamon/ScreenSaver"},
      {"org.mate.ScreenSaver", "/org/mate/ScreenSaver"},
      {"org.xfce.ScreenSaver", "/org/xfce/ScreenSaver"},
  };
  bool accepted = false;
  for (const auto& service : services) {
    DBusMessage* message = dbus_message_new_method_call(
        service.name, service.path, service.name, "Lock");
    if (!message) break;
    dbus_message_set_auto_start(message, false);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        connection, message, 1000, &error);
    dbus_message_unref(message);
    if (reply) {
      accepted = dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN;
      dbus_message_unref(reply);
    }
    dbus_error_free(&error);
    if (accepted) break;
  }
  dbus_connection_close(connection);
  dbus_connection_unref(connection);
  if (!accepted) LOG_WARN("No desktop screen lock service accepted the request");
  return accepted;
}

}  // namespace crossdesk
