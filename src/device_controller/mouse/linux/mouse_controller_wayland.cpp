#include "mouse_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
#include <dbus/dbus.h>
#endif

#include "platform.h"
#include "rd_log.h"
#include "wayland_portal_shared.h"

namespace crossdesk {

void MouseController::OnWaylandDisplayInfoListUpdated() {
  const uintptr_t stream0 =
      display_info_list_.empty()
          ? 0
          : reinterpret_cast<uintptr_t>(display_info_list_[0].handle);
  const int width0 = display_info_list_.empty() ? 0 : display_info_list_[0].width;
  const int height0 =
      display_info_list_.empty() ? 0 : display_info_list_[0].height;
  const bool should_log = !logged_wayland_display_info_ ||
                          stream0 != last_logged_wayland_stream_ ||
                          width0 != last_logged_wayland_width_ ||
                          height0 != last_logged_wayland_height_;

  if (!should_log) {
    return;
  }

  logged_wayland_display_info_ = true;
  last_logged_wayland_stream_ = stream0;
  last_logged_wayland_width_ = width0;
  last_logged_wayland_height_ = height0;

  for (size_t i = 0; i < display_info_list_.size(); ++i) {
    const auto& display = display_info_list_[i];
    LOG_INFO(
        "Wayland mouse display info [{}]: name={}, rect=({},{})->({},{}) "
        "size={}x{}, stream={}"
        ,
        i, display.name, display.left, display.top, display.right,
        display.bottom, display.width, display.height,
        reinterpret_cast<uintptr_t>(display.handle));
  }
}

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
namespace {

constexpr const char* kPortalBusName = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalObjectPath = "/org/freedesktop/portal/desktop";
constexpr const char* kPortalRemoteDesktopInterface =
    "org.freedesktop.portal.RemoteDesktop";
constexpr const char* kPortalScreenCastInterface =
    "org.freedesktop.portal.ScreenCast";
constexpr const char* kPortalRequestInterface =
    "org.freedesktop.portal.Request";
constexpr const char* kPortalSessionInterface =
    "org.freedesktop.portal.Session";
constexpr const char* kPortalRequestPathPrefix =
    "/org/freedesktop/portal/desktop/request/";
constexpr const char* kPortalSessionPathPrefix =
    "/org/freedesktop/portal/desktop/session/";

constexpr uint32_t kRemoteDesktopDevicePointer = 2u;
constexpr uint32_t kScreenCastSourceMonitor = 1u;

constexpr uint32_t kPointerReleased = 0u;
constexpr uint32_t kPointerPressed = 1u;

constexpr uint32_t kPointerAxisVertical = 0u;
constexpr uint32_t kPointerAxisHorizontal = 1u;

constexpr int kBtnLeft = 0x110;
constexpr int kBtnRight = 0x111;
constexpr int kBtnMiddle = 0x112;

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

void AppendDictEntryBool(DBusMessageIter* dict, const char* key, bool value) {
  DBusMessageIter entry;
  DBusMessageIter variant;
  const char* key_cstr = key;
  dbus_bool_t bool_value = value ? TRUE : FALSE;

  dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_cstr);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &bool_value);
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

bool ReadFirstStreamId(DBusMessageIter* variant, uint32_t* stream_id) {
  if (!variant || !stream_id) {
    return false;
  }
  if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_ARRAY) {
    return false;
  }

  DBusMessageIter streams;
  dbus_message_iter_recurse(variant, &streams);
  while (dbus_message_iter_get_arg_type(&streams) != DBUS_TYPE_INVALID) {
    if (dbus_message_iter_get_arg_type(&streams) == DBUS_TYPE_STRUCT) {
      DBusMessageIter stream;
      dbus_message_iter_recurse(&streams, &stream);
      uint32_t candidate = 0;
      if (ReadUint32Like(&stream, &candidate) && candidate != 0) {
        *stream_id = candidate;
        return true;
      }
    }
    dbus_message_iter_next(&streams);
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
    const char* method_name,
    const char* action_name,
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

bool MouseController::InitWaylandPortal() {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  CleanupWaylandPortal();

  auto attach_shared_session =
      [this](const SharedWaylandPortalSessionInfo& shared_session) {
        dbus_connection_ = shared_session.connection;
        wayland_session_handle_ = shared_session.session_handle;
        wayland_absolute_stream_id_ = shared_session.stream_id;
        last_display_index_ = -1;
        last_norm_x_ = -1.0;
        last_norm_y_ = -1.0;
        logged_wayland_display_info_ = false;
        last_logged_wayland_stream_ = 0;
        last_logged_wayland_width_ = 0;
        last_logged_wayland_height_ = 0;
        wayland_absolute_mode_ = WaylandAbsoluteMode::kUnknown;
        wayland_absolute_disabled_logged_ = false;
        using_shared_wayland_session_ = true;
        LOG_INFO("Mouse controller attached to shared Wayland portal session, "
                 "stream_id={}",
                 wayland_absolute_stream_id_);
        return true;
      };

  SharedWaylandPortalSessionInfo shared_session;
  if (AcquireSharedWaylandPortalSession(true, &shared_session)) {
    return attach_shared_session(shared_session);
  }

  const auto wait_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
  bool waiting_logged = false;
  while (std::chrono::steady_clock::now() < wait_deadline) {
    if (AcquireSharedWaylandPortalSession(true, &shared_session)) {
      return attach_shared_session(shared_session);
    }

    if (!waiting_logged) {
      waiting_logged = true;
      LOG_INFO("Waiting for shared Wayland portal session from screen "
               "capturer before creating a standalone mouse session");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (waiting_logged) {
    LOG_WARN("Shared Wayland portal session did not appear in time; falling "
             "back to standalone mouse portal session");
  }

  if (AcquireSharedWaylandPortalSession(true, &shared_session)) {
    return attach_shared_session(shared_session);
  }

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

  const std::string session_handle_token = MakeToken("crossdesk_mouse_session");
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
                              MakeToken("crossdesk_mouse_req"));
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
        AppendDictEntryUint32(&options, "types", kRemoteDesktopDevicePointer);
        AppendDictEntryString(&options, "handle_token",
                              MakeToken("crossdesk_mouse_req"));
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

  const bool select_sources_ok = SendPortalRequestAndHandleResponse(
      dbus_connection_, kPortalScreenCastInterface, "SelectSources",
      "SelectSources",
      [&](DBusMessage* message) {
        DBusMessageIter iter;
        DBusMessageIter options;
        dbus_message_iter_init_append(message, &iter);
        dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                         &options);
        AppendDictEntryUint32(&options, "types", kScreenCastSourceMonitor);
        AppendDictEntryBool(&options, "multiple", false);
        AppendDictEntryString(&options, "handle_token",
                              MakeToken("crossdesk_mouse_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      [](uint32_t response_code, DBusMessageIter*) {
        if (response_code != 0) {
          LOG_ERROR("ScreenCast.SelectSources denied, response={}",
                    response_code);
          return false;
        }
        return true;
      });

  if (!select_sources_ok) {
    CleanupWaylandPortal();
    return false;
  }

  const char* parent_window = "";
  bool pointer_granted = false;
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
                              MakeToken("crossdesk_mouse_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      [&](uint32_t response_code, DBusMessageIter* results) {
        if (response_code != 0) {
          LOG_ERROR("RemoteDesktop.Start denied, response={}", response_code);
          return false;
        }

        uint32_t granted_devices = 0;
        uint32_t absolute_stream_id = 0;
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
              } else if (strcmp(key, "streams") == 0) {
                ReadFirstStreamId(&variant, &absolute_stream_id);
              }
            }
          }
          dbus_message_iter_next(&dict);
        }

        pointer_granted = (granted_devices & kRemoteDesktopDevicePointer) != 0;
        if (!pointer_granted) {
          LOG_ERROR(
              "RemoteDesktop.Start granted devices mask={}, pointer not allowed",
              granted_devices);
          return false;
        }
        if (absolute_stream_id == 0) {
          LOG_ERROR("RemoteDesktop.Start did not return a screencast stream id");
          return false;
        }
        wayland_absolute_stream_id_ = absolute_stream_id;
        wayland_absolute_mode_ = WaylandAbsoluteMode::kUnknown;
        wayland_absolute_disabled_logged_ = false;
        LOG_INFO("Wayland mouse absolute stream id={}",
                 wayland_absolute_stream_id_);
        return true;
      });

  if (!start_ok) {
    CleanupWaylandPortal();
    return false;
  }

  if (!pointer_granted) {
    LOG_ERROR("RemoteDesktop session started without pointer permission");
    CleanupWaylandPortal();
    return false;
  }
  if (wayland_absolute_stream_id_ == 0) {
    LOG_ERROR("Wayland absolute stream id is missing after Start");
    CleanupWaylandPortal();
    return false;
  }

  last_display_index_ = -1;
  last_norm_x_ = -1.0;
  last_norm_y_ = -1.0;
  logged_wayland_display_info_ = false;
  last_logged_wayland_stream_ = 0;
  last_logged_wayland_width_ = 0;
  last_logged_wayland_height_ = 0;
  wayland_absolute_mode_ = WaylandAbsoluteMode::kUnknown;
  wayland_absolute_disabled_logged_ = false;
  return true;
#else
  return false;
#endif
}

void MouseController::CleanupWaylandPortal() {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  if (using_shared_wayland_session_) {
    DBusConnection* close_connection = nullptr;
    std::string close_session_handle;
    ReleaseSharedWaylandPortalSession(&close_connection, &close_session_handle);
    if (close_connection) {
      CloseWaylandPortalSessionAndConnection(close_connection,
                                             close_session_handle,
                                             "RemoteDesktop.Session.Close");
    }
    dbus_connection_ = nullptr;
  } else if (dbus_connection_) {
    CloseWaylandPortalSessionAndConnection(dbus_connection_,
                                           wayland_session_handle_,
                                           "RemoteDesktop.Session.Close");
    dbus_connection_ = nullptr;
  }
#endif

  use_wayland_portal_ = false;
  wayland_session_handle_.clear();
  last_display_index_ = -1;
  last_norm_x_ = -1.0;
  last_norm_y_ = -1.0;
  logged_wayland_display_info_ = false;
  last_logged_wayland_stream_ = 0;
  last_logged_wayland_width_ = 0;
  last_logged_wayland_height_ = 0;
  wayland_absolute_mode_ = WaylandAbsoluteMode::kUnknown;
  wayland_absolute_disabled_logged_ = false;
  wayland_absolute_stream_id_ = 0;
  using_shared_wayland_session_ = false;
}

int MouseController::SendWaylandMouseCommand(RemoteAction remote_action,
                                             int display_index) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  if (!dbus_connection_ || wayland_session_handle_.empty()) {
    return -1;
  }

  switch (remote_action.m.flag) {
    case MouseFlag::move: {
      if (display_index < 0 ||
          display_index >= static_cast<int>(display_info_list_.size())) {
        LOG_ERROR("Invalid display index for Wayland mouse move: {}",
                  display_index);
        return -2;
      }
      if (wayland_absolute_stream_id_ == 0) {
        if (!wayland_absolute_disabled_logged_) {
          wayland_absolute_disabled_logged_ = true;
          LOG_ERROR(
              "Wayland absolute pointer stream id is missing; cannot send "
              "pointer motion");
        }
        return -3;
      }
      if (wayland_absolute_mode_ == WaylandAbsoluteMode::kDisabled) {
        if (!wayland_absolute_disabled_logged_) {
          wayland_absolute_disabled_logged_ = true;
          LOG_ERROR(
              "Wayland absolute pointer mode is unavailable for current "
              "portal session");
        }
        return -3;
      }

      const DisplayInfo& display_info = display_info_list_[display_index];
      const int width = std::max(display_info.width, 1);
      const int height = std::max(display_info.height, 1);

      const double norm_x =
          std::clamp(static_cast<double>(remote_action.m.x), 0.0, 1.0);
      const double norm_y =
          std::clamp(static_cast<double>(remote_action.m.y), 0.0, 1.0);

      if (last_display_index_ == display_index &&
          std::abs(norm_x - last_norm_x_) < 1e-6 &&
          std::abs(norm_y - last_norm_y_) < 1e-6) {
        return 0;
      }

      const uint32_t stream = wayland_absolute_stream_id_;
      const double abs_x = norm_x * std::max(width - 1, 1);
      const double abs_y = norm_y * std::max(height - 1, 1);

      auto accept_absolute = [&]() {
        last_display_index_ = display_index;
        last_norm_x_ = norm_x;
        last_norm_y_ = norm_y;
        return 0;
      };

      if (wayland_absolute_mode_ == WaylandAbsoluteMode::kPixels) {
        if (NotifyWaylandPointerMotionAbsolute(stream, abs_x, abs_y)) {
          return accept_absolute();
        }
        wayland_absolute_mode_ = WaylandAbsoluteMode::kDisabled;
      } else if (wayland_absolute_mode_ == WaylandAbsoluteMode::kNormalized) {
        if (NotifyWaylandPointerMotionAbsolute(stream, norm_x, norm_y)) {
          return accept_absolute();
        }
        wayland_absolute_mode_ = WaylandAbsoluteMode::kDisabled;
      } else {
        if (NotifyWaylandPointerMotionAbsolute(stream, abs_x, abs_y)) {
          wayland_absolute_mode_ = WaylandAbsoluteMode::kPixels;
          LOG_INFO("Wayland absolute pointer mode selected: pixel coordinates");
          return accept_absolute();
        }

        if (NotifyWaylandPointerMotionAbsolute(stream, norm_x, norm_y)) {
          wayland_absolute_mode_ = WaylandAbsoluteMode::kNormalized;
          LOG_INFO(
              "Wayland absolute pointer mode selected: normalized "
              "coordinates");
          return accept_absolute();
        }

        wayland_absolute_mode_ = WaylandAbsoluteMode::kDisabled;
      }

      if (!wayland_absolute_disabled_logged_) {
        wayland_absolute_disabled_logged_ = true;
        LOG_ERROR(
            "NotifyPointerMotionAbsolute rejected by portal backend in both "
            "pixel and normalized modes");
      }
      return -3;
    }
    case MouseFlag::left_down:
      if (!NotifyWaylandPointerButton(kBtnLeft, kPointerPressed)) {
        return -3;
      }
      break;
    case MouseFlag::left_up:
      if (!NotifyWaylandPointerButton(kBtnLeft, kPointerReleased)) {
        return -3;
      }
      break;
    case MouseFlag::right_down:
      if (!NotifyWaylandPointerButton(kBtnRight, kPointerPressed)) {
        return -3;
      }
      break;
    case MouseFlag::right_up:
      if (!NotifyWaylandPointerButton(kBtnRight, kPointerReleased)) {
        return -3;
      }
      break;
    case MouseFlag::middle_down:
      if (!NotifyWaylandPointerButton(kBtnMiddle, kPointerPressed)) {
        return -3;
      }
      break;
    case MouseFlag::middle_up:
      if (!NotifyWaylandPointerButton(kBtnMiddle, kPointerReleased)) {
        return -3;
      }
      break;
    case MouseFlag::wheel_vertical: {
      if (remote_action.m.s == 0) {
        return 0;
      }
      if (!NotifyWaylandPointerAxisDiscrete(kPointerAxisVertical,
                                            remote_action.m.s)) {
        return -3;
      }
      break;
    }
    case MouseFlag::wheel_horizontal: {
      if (remote_action.m.s == 0) {
        return 0;
      }
      if (!NotifyWaylandPointerAxisDiscrete(kPointerAxisHorizontal,
                                            remote_action.m.s)) {
        return -3;
      }
      break;
    }
  }

  return 0;
#else
  (void)remote_action;
  (void)display_index;
  return -1;
#endif
}

bool MouseController::NotifyWaylandPointerMotion(double dx, double dy) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  return SendWaylandPortalVoidCall(
      "NotifyPointerMotion", [&](DBusMessageIter* iter) {
        const char* session_handle = wayland_session_handle_.c_str();
        dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        AppendEmptyOptionsDict(iter);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &dx);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &dy);
      });
#else
  (void)dx;
  (void)dy;
  return false;
#endif
}

bool MouseController::NotifyWaylandPointerMotionAbsolute(uint32_t stream,
                                                         double x, double y) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  return SendWaylandPortalVoidCall(
      "NotifyPointerMotionAbsolute", [&](DBusMessageIter* iter) {
        const char* session_handle = wayland_session_handle_.c_str();
        uint32_t stream_id = stream;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        AppendEmptyOptionsDict(iter);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &stream_id);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &x);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &y);
      });
#else
  (void)stream;
  (void)x;
  (void)y;
  return false;
#endif
}

bool MouseController::NotifyWaylandPointerButton(int button, uint32_t state) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  return SendWaylandPortalVoidCall(
      "NotifyPointerButton", [&](DBusMessageIter* iter) {
        const char* session_handle = wayland_session_handle_.c_str();
        int32_t btn = button;
        uint32_t btn_state = state;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        AppendEmptyOptionsDict(iter);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &btn);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &btn_state);
      });
#else
  (void)button;
  (void)state;
  return false;
#endif
}

bool MouseController::NotifyWaylandPointerAxisDiscrete(uint32_t axis,
                                                       int32_t steps) {
#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER
  return SendWaylandPortalVoidCall(
      "NotifyPointerAxisDiscrete", [&](DBusMessageIter* iter) {
        const char* session_handle = wayland_session_handle_.c_str();
        uint32_t axis_id = axis;
        int32_t discrete_steps = steps;
        dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
                                       &session_handle);
        AppendEmptyOptionsDict(iter);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &axis_id);
        dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &discrete_steps);
      });
#else
  (void)axis;
  (void)steps;
  return false;
#endif
}

bool MouseController::SendWaylandPortalVoidCall(
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
