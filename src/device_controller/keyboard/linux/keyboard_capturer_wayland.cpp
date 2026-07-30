#include "keyboard_capturer.h"

#include <chrono>
#include <cstring>
#include <map>

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <dbus/dbus.h>
#endif

#include "linux_evdev_keycode.h"
#include "rd_log.h"
#include "wayland_portal_shared.h"

namespace crossdesk {

extern std::map<int, int> vkCodeToX11KeySym;

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
namespace {

constexpr const char* kPortalBusName = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalObjectPath = "/org/freedesktop/portal/desktop";
constexpr const char* kPortalRemoteDesktopInterface =
    "org.freedesktop.portal.RemoteDesktop";
constexpr const char* kPortalRequestInterface =
    "org.freedesktop.portal.Request";
constexpr const char* kPortalRequestPathPrefix =
    "/org/freedesktop/portal/desktop/request/";
constexpr const char* kPortalSessionPathPrefix =
    "/org/freedesktop/portal/desktop/session/";

constexpr uint32_t kRemoteDesktopDeviceKeyboard = 1u;
constexpr uint32_t kKeyboardReleased = 0u;
constexpr uint32_t kKeyboardPressed = 1u;

int NormalizeFallbackKeysym(int keysym) {
  if (keysym >= XK_A && keysym <= XK_Z) {
    return keysym - XK_A + XK_a;
  }
  return keysym;
}

std::string MakeToken(const char* prefix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::string(prefix) + "_" + std::to_string(now);
}

void LogDbusError(const char* action, DBusError* error) {
  if (error && dbus_error_is_set(error)) {
    LOG_ERROR("{} failed: {} ({})", action,
              error->message ? error->message : "unknown",
              error->name ? error->name : "unknown");
  } else {
    LOG_ERROR("{} failed", action);
  }
}

void AppendDictEntryString(DBusMessageIter* dict, const char* key,
                           const std::string& value) {
  DBusMessageIter entry;
  DBusMessageIter variant;
  const char* key_cstr = key;
  const char* value_cstr = value.c_str();

  dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_cstr);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value_cstr);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(dict, &entry);
}

void AppendDictEntryUint32(DBusMessageIter* dict, const char* key,
                           uint32_t value) {
  DBusMessageIter entry;
  DBusMessageIter variant;
  const char* key_cstr = key;

  dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_cstr);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(dict, &entry);
}

void AppendEmptyOptionsDict(DBusMessageIter* iter) {
  DBusMessageIter options;
  dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &options);
  dbus_message_iter_close_container(iter, &options);
}

bool ReadPathLikeVariant(DBusMessageIter* variant, std::string* value) {
  if (!variant || !value) {
    return false;
  }

  const int type = dbus_message_iter_get_arg_type(variant);
  if (type == DBUS_TYPE_OBJECT_PATH || type == DBUS_TYPE_STRING) {
    const char* temp = nullptr;
    dbus_message_iter_get_basic(variant, &temp);
    if (temp && temp[0] != '\0') {
      *value = temp;
      return true;
    }
  }

  return false;
}

bool ReadUint32Like(DBusMessageIter* iter, uint32_t* value) {
  if (!iter || !value) {
    return false;
  }

  const int type = dbus_message_iter_get_arg_type(iter);
  if (type == DBUS_TYPE_UINT32) {
    uint32_t temp = 0;
    dbus_message_iter_get_basic(iter, &temp);
    *value = temp;
    return true;
  }

  if (type == DBUS_TYPE_INT32) {
    int32_t temp = 0;
    dbus_message_iter_get_basic(iter, &temp);
    if (temp < 0) {
      return false;
    }
    *value = static_cast<uint32_t>(temp);
    return true;
  }

  return false;
}

std::string BuildSessionHandleFromRequestPath(
    const std::string& request_path, const std::string& session_handle_token) {
  if (request_path.rfind(kPortalRequestPathPrefix, 0) != 0 ||
      session_handle_token.empty()) {
    return "";
  }

  const size_t sender_start = strlen(kPortalRequestPathPrefix);
  const size_t token_sep = request_path.find('/', sender_start);
  if (token_sep == std::string::npos || token_sep <= sender_start) {
    return "";
  }

  const std::string sender =
      request_path.substr(sender_start, token_sep - sender_start);
  if (sender.empty()) {
    return "";
  }

  return std::string(kPortalSessionPathPrefix) + sender + "/" +
         session_handle_token;
}

struct PortalResponseState {
  std::string request_path;
  bool received = false;
  DBusMessage* message = nullptr;
};

DBusHandlerResult HandlePortalResponseSignal(DBusConnection* connection,
                                             DBusMessage* message,
                                             void* user_data) {
  (void)connection;
  auto* state = static_cast<PortalResponseState*>(user_data);
  if (!state || !message) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (!dbus_message_is_signal(message, kPortalRequestInterface, "Response")) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  const char* path = dbus_message_get_path(message);
  if (!path || state->request_path != path) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (state->message) {
    dbus_message_unref(state->message);
    state->message = nullptr;
  }

  state->message = dbus_message_ref(message);
  state->received = true;
  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusMessage* WaitForPortalResponse(DBusConnection* connection,
                                   const std::string& request_path,
                                   int timeout_ms = 120000) {
  if (!connection || request_path.empty()) {
    return nullptr;
  }

  PortalResponseState state;
  state.request_path = request_path;

  DBusError error;
  dbus_error_init(&error);

  const std::string match_rule =
      "type='signal',interface='" + std::string(kPortalRequestInterface) +
      "',member='Response',path='" + request_path + "'";
  dbus_bus_add_match(connection, match_rule.c_str(), &error);
  if (dbus_error_is_set(&error)) {
    LogDbusError("dbus_bus_add_match", &error);
    dbus_error_free(&error);
    return nullptr;
  }

  dbus_connection_add_filter(connection, HandlePortalResponseSignal, &state,
                             nullptr);

  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!state.received && std::chrono::steady_clock::now() < deadline) {
    dbus_connection_read_write(connection, 100);
    while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS) {
    }
  }

  dbus_connection_remove_filter(connection, HandlePortalResponseSignal, &state);

  DBusError remove_error;
  dbus_error_init(&remove_error);
  dbus_bus_remove_match(connection, match_rule.c_str(), &remove_error);
  if (dbus_error_is_set(&remove_error)) {
    dbus_error_free(&remove_error);
  }

  return state.message;
}

bool ExtractRequestPath(DBusMessage* reply, std::string* request_path) {
  if (!reply || !request_path) {
    return false;
  }

  const char* path = nullptr;
  DBusError error;
  dbus_error_init(&error);
  const dbus_bool_t ok = dbus_message_get_args(
      reply, &error, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
  if (!ok || !path) {
    LogDbusError("dbus_message_get_args(request_path)", &error);
    dbus_error_free(&error);
    return false;
  }

  *request_path = path;
  return true;
}

bool ExtractPortalResponse(DBusMessage* message, uint32_t* response_code,
                           DBusMessageIter* results_array) {
  if (!message || !response_code || !results_array) {
    return false;
  }

  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter) ||
      dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
    return false;
  }

  dbus_message_iter_get_basic(&iter, response_code);
  if (!dbus_message_iter_next(&iter) ||
      dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
    return false;
  }

  *results_array = iter;
  return true;
}

bool SendPortalRequestAndHandleResponse(
    DBusConnection* connection, const char* interface_name,
    const char* method_name, const char* action_name,
    const std::function<bool(DBusMessage*)>& append_message_args,
    const std::function<bool(uint32_t, DBusMessageIter*)>& handle_results,
    std::string* request_path_out = nullptr) {
  if (!connection || !interface_name || interface_name[0] == '\0' ||
      !method_name || method_name[0] == '\0') {
    return false;
  }

  DBusMessage* message = dbus_message_new_method_call(
      kPortalBusName, kPortalObjectPath, interface_name, method_name);
  if (!message) {
    LOG_ERROR("Failed to allocate {} message", method_name);
    return false;
  }

  if (append_message_args && !append_message_args(message)) {
    dbus_message_unref(message);
    LOG_ERROR("{} arguments are malformed", method_name);
    return false;
  }

  DBusError error;
  dbus_error_init(&error);
  DBusMessage* reply =
      dbus_connection_send_with_reply_and_block(connection, message, -1, &error);
  dbus_message_unref(message);
  if (!reply) {
    LogDbusError(action_name ? action_name : method_name, &error);
    dbus_error_free(&error);
    return false;
  }

  std::string request_path;
  const bool got_request_path = ExtractRequestPath(reply, &request_path);
  dbus_message_unref(reply);
  if (!got_request_path) {
    return false;
  }
  if (request_path_out) {
    *request_path_out = request_path;
  }

  DBusMessage* response = WaitForPortalResponse(connection, request_path);
  if (!response) {
    LOG_ERROR("Timed out waiting for {} response", method_name);
    return false;
  }

  uint32_t response_code = 1;
  DBusMessageIter results;
  const bool parsed = ExtractPortalResponse(response, &response_code, &results);
  if (!parsed) {
    dbus_message_unref(response);
    LOG_ERROR("{} response was malformed", method_name);
    return false;
  }

  const bool ok = handle_results ? handle_results(response_code, &results)
                                 : (response_code == 0);
  dbus_message_unref(response);
  return ok;
}

}  // namespace
#endif

bool KeyboardCapturer::InitWaylandPortal() {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  CleanupWaylandPortal();

  DBusError error;
  dbus_error_init(&error);

  DBusConnection* check_connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
  if (!check_connection) {
    LogDbusError("dbus_bus_get", &error);
    dbus_error_free(&error);
    return false;
  }

  const dbus_bool_t has_owner =
      dbus_bus_name_has_owner(check_connection, kPortalBusName, &error);
  if (dbus_error_is_set(&error)) {
    LogDbusError("dbus_bus_name_has_owner", &error);
    dbus_error_free(&error);
    dbus_connection_unref(check_connection);
    return false;
  }
  dbus_connection_unref(check_connection);

  if (!has_owner) {
    LOG_ERROR("xdg-desktop-portal is not available on session bus");
    return false;
  }

  dbus_connection_ = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
  if (!dbus_connection_) {
    LogDbusError("dbus_bus_get_private", &error);
    dbus_error_free(&error);
    return false;
  }
  dbus_connection_set_exit_on_disconnect(dbus_connection_, FALSE);

  const std::string session_handle_token =
      MakeToken("crossdesk_keyboard_session");
  std::string request_path;
  const bool create_ok = SendPortalRequestAndHandleResponse(
      dbus_connection_, kPortalRemoteDesktopInterface, "CreateSession",
      "CreateSession",
      [&](DBusMessage* message) {
        DBusMessageIter iter;
        DBusMessageIter options;
        dbus_message_iter_init_append(message, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                         &options);
        AppendDictEntryString(&options, "session_handle_token",
                              session_handle_token);
        AppendDictEntryString(&options, "handle_token",
                              MakeToken("crossdesk_keyboard_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      [&](uint32_t response_code, DBusMessageIter* results) {
        if (response_code != 0) {
          LOG_ERROR("RemoteDesktop.CreateSession denied, response={}",
                    response_code);
          return false;
        }

        DBusMessageIter dict;
        dbus_message_iter_recurse(results, &dict);
        while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
          if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&dict, &entry);

            const char* key = nullptr;
            dbus_message_iter_get_basic(&entry, &key);
            if (key && dbus_message_iter_next(&entry) &&
                dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT &&
                strcmp(key, "session_handle") == 0) {
              DBusMessageIter variant;
              std::string parsed_handle;
              dbus_message_iter_recurse(&entry, &variant);
              if (ReadPathLikeVariant(&variant, &parsed_handle) &&
                  !parsed_handle.empty()) {
                wayland_session_handle_ = parsed_handle;
                break;
              }
            }
          }
          dbus_message_iter_next(&dict);
        }
        return true;
      },
      &request_path);

  if (!create_ok) {
    CleanupWaylandPortal();
    return false;
  }

  if (wayland_session_handle_.empty()) {
    wayland_session_handle_ =
        BuildSessionHandleFromRequestPath(request_path, session_handle_token);
  }

  if (wayland_session_handle_.empty()) {
    LOG_ERROR("RemoteDesktop.CreateSession did not return session handle");
    CleanupWaylandPortal();
    return false;
  }

  const char* session_handle = wayland_session_handle_.c_str();
  const bool select_ok = SendPortalRequestAndHandleResponse(
      dbus_connection_, kPortalRemoteDesktopInterface, "SelectDevices",
      "SelectDevices",
      [&](DBusMessage* message) {
        DBusMessageIter iter;
        DBusMessageIter options;
        dbus_message_iter_init_append(message, &iter);
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                         &options);
        AppendDictEntryUint32(&options, "types", kRemoteDesktopDeviceKeyboard);
        AppendDictEntryString(&options, "handle_token",
                              MakeToken("crossdesk_keyboard_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      [](uint32_t response_code, DBusMessageIter*) {
        if (response_code != 0) {
          LOG_ERROR("RemoteDesktop.SelectDevices denied, response={}",
                    response_code);
          return false;
        }
        return true;
      });

  if (!select_ok) {
    CleanupWaylandPortal();
    return false;
  }

  const char* parent_window = "";
  bool keyboard_granted = false;
  const bool start_ok = SendPortalRequestAndHandleResponse(
      dbus_connection_, kPortalRemoteDesktopInterface, "Start", "Start",
      [&](DBusMessage* message) {
        DBusMessageIter iter;
        DBusMessageIter options;
        dbus_message_iter_init_append(message, &iter);
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_window);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                         &options);
        AppendDictEntryString(&options, "handle_token",
                              MakeToken("crossdesk_keyboard_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      [&](uint32_t response_code, DBusMessageIter* results) {
        if (response_code != 0) {
          LOG_ERROR("RemoteDesktop.Start denied, response={}", response_code);
          return false;
        }

        uint32_t granted_devices = 0;
        DBusMessageIter dict;
        dbus_message_iter_recurse(results, &dict);
        while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
          if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&dict, &entry);

            const char* key = nullptr;
            dbus_message_iter_get_basic(&entry, &key);
            if (key && dbus_message_iter_next(&entry) &&
                dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
              DBusMessageIter variant;
              dbus_message_iter_recurse(&entry, &variant);
              if (strcmp(key, "devices") == 0) {
                ReadUint32Like(&variant, &granted_devices);
              }
            }
          }
          dbus_message_iter_next(&dict);
        }

        keyboard_granted =
            (granted_devices & kRemoteDesktopDeviceKeyboard) != 0;
        if (!keyboard_granted) {
          LOG_ERROR(
              "RemoteDesktop.Start granted devices mask={}, keyboard not allowed",
              granted_devices);
          return false;
        }
        return true;
      });

  if (!start_ok) {
    CleanupWaylandPortal();
    return false;
  }

  if (!keyboard_granted) {
    LOG_ERROR("RemoteDesktop session started without keyboard permission");
    CleanupWaylandPortal();
    return false;
  }

  return true;
#else
  return false;
#endif
}

void KeyboardCapturer::CleanupWaylandPortal() {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  if (dbus_connection_) {
    CloseWaylandPortalSessionAndConnection(dbus_connection_,
                                           wayland_session_handle_,
                                           "RemoteDesktop.Session.Close");
    dbus_connection_ = nullptr;
  }
#endif

  use_wayland_portal_ = false;
  wayland_session_handle_.clear();
}

int KeyboardCapturer::SendWaylandKeyboardCommand(int key_code, bool is_down,
                                                 uint32_t scan_code,
                                                 bool extended) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  if (!dbus_connection_ || wayland_session_handle_.empty()) {
    return -1;
  }

  const uint32_t key_state = is_down ? kKeyboardPressed : kKeyboardReleased;

  const int evdev_keycode =
      ResolveLinuxEvdevKeycodeFromWindowsKey(key_code, scan_code, extended);
  if (evdev_keycode >= 0 &&
      NotifyWaylandKeyboardKeycode(evdev_keycode, key_state)) {
    return 0;
  }

  const auto key_it = vkCodeToX11KeySym.find(key_code);
  if (key_it == vkCodeToX11KeySym.end()) {
    if (evdev_keycode >= 0) {
      LOG_ERROR(
          "Failed to send Wayland keyboard keycode event, vk_code={}, "
          "evdev_keycode={}, is_down={}",
          key_code, evdev_keycode, is_down);
      return -3;
    }
    return 0;
  }

  // Prefer keycode injection to preserve physical-key semantics and avoid
  // implicit Shift interpretation for uppercase keysyms.
  if (display_) {
    const int keysym = key_it->second;
    const KeyCode x11_keycode =
        XKeysymToKeycode(display_, static_cast<KeySym>(keysym));
    if (x11_keycode > 8) {
      const int x11_evdev_keycode = static_cast<int>(x11_keycode) - 8;
      if (NotifyWaylandKeyboardKeycode(x11_evdev_keycode, key_state)) {
        return 0;
      }
    }
  }

  const int keysym = key_it->second;
  const int fallback_keysym = NormalizeFallbackKeysym(keysym);
  if (NotifyWaylandKeyboardKeysym(fallback_keysym, key_state)) {
    return 0;
  }

  LOG_ERROR("Failed to send Wayland keyboard event, vk_code={}, is_down={}",
            key_code, is_down);
  return -3;
#else
  (void)key_code;
  (void)is_down;
  (void)scan_code;
  (void)extended;
  return -1;
#endif
}

bool KeyboardCapturer::NotifyWaylandKeyboardKeysym(int keysym, uint32_t state) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  return SendWaylandPortalVoidCall(
      "NotifyKeyboardKeysym", [&](DBusMessageIter* iter) {
        const char* session_handle = wayland_session_handle_.c_str();
        int32_t key_sym = keysym;
        uint32_t key_state = state;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        AppendEmptyOptionsDict(iter);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &key_sym);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &key_state);
      });
#else
  (void)keysym;
  (void)state;
  return false;
#endif
}

bool KeyboardCapturer::NotifyWaylandKeyboardKeycode(int keycode,
                                                    uint32_t state) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  return SendWaylandPortalVoidCall(
      "NotifyKeyboardKeycode", [&](DBusMessageIter* iter) {
        const char* session_handle = wayland_session_handle_.c_str();
        int32_t key_code = keycode;
        uint32_t key_state = state;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        AppendEmptyOptionsDict(iter);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &key_code);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &key_state);
      });
#else
  (void)keycode;
  (void)state;
  return false;
#endif
}

bool KeyboardCapturer::SendWaylandPortalVoidCall(
    const char* method_name,
    const std::function<void(DBusMessageIter*)>& append_args) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  if (!dbus_connection_ || !method_name || method_name[0] == '\0') {
    return false;
  }

  DBusMessage* message = dbus_message_new_method_call(
      kPortalBusName, kPortalObjectPath, kPortalRemoteDesktopInterface,
      method_name);
  if (!message) {
    LOG_ERROR("Failed to allocate {} message", method_name);
    return false;
  }

  DBusMessageIter iter;
  dbus_message_iter_init_append(message, &iter);
  if (append_args) {
    append_args(&iter);
  }

  DBusError error;
  dbus_error_init(&error);
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(
      dbus_connection_, message, 5000, &error);
  dbus_message_unref(message);
  if (!reply) {
    LogDbusError(method_name, &error);
    dbus_error_free(&error);
    return false;
  }

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    const char* error_name = dbus_message_get_error_name(reply);
    LOG_ERROR("{} returned DBus error: {}", method_name,
              error_name ? error_name : "unknown");
    dbus_message_unref(reply);
    return false;
  }

  dbus_message_unref(reply);
  return true;
#else
  (void)method_name;
  (void)append_args;
  return false;
#endif
}

}  // namespace crossdesk
