#include "screen_capturer_wayland.h"
#include "screen_capturer_wayland_build.h"
#include "wayland_portal_shared.h"

#if CROSSDESK_WAYLAND_BUILD_ENABLED

#include <unistd.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <string>

#include "rd_log.h"

namespace crossdesk {

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

constexpr uint32_t kScreenCastSourceMonitor = 1u;
constexpr uint32_t kCursorModeHidden = 1u;
constexpr uint32_t kCursorModeEmbedded = 2u;
constexpr uint32_t kRemoteDesktopDevicePointer = 2u;

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

bool ReadIntLike(DBusMessageIter* iter, int* value) {
  if (!iter || !value) {
    return false;
  }

  const int type = dbus_message_iter_get_arg_type(iter);
  if (type == DBUS_TYPE_INT32) {
    int32_t temp = 0;
    dbus_message_iter_get_basic(iter, &temp);
    *value = static_cast<int>(temp);
    return true;
  }

  if (type == DBUS_TYPE_UINT32) {
    uint32_t temp = 0;
    dbus_message_iter_get_basic(iter, &temp);
    *value = static_cast<int>(temp);
    return true;
  }

  return false;
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
                                   const std::atomic<bool>& running,
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
  while (running.load() && !state.received &&
         std::chrono::steady_clock::now() < deadline) {
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
    const std::atomic<bool>& running,
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
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(
      connection, message, -1, &error);
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

  DBusMessage* response =
      WaitForPortalResponse(connection, request_path, running);
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

bool ScreenCapturerWayland::CheckPortalAvailability() const {
  DBusError error;
  dbus_error_init(&error);

  DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
  if (!connection) {
    LogDbusError("dbus_bus_get", &error);
    dbus_error_free(&error);
    return false;
  }

  const dbus_bool_t has_owner =
      dbus_bus_name_has_owner(connection, kPortalBusName, &error);
  if (dbus_error_is_set(&error)) {
    LogDbusError("dbus_bus_name_has_owner", &error);
    dbus_error_free(&error);
    dbus_connection_unref(connection);
    return false;
  }

  dbus_connection_unref(connection);
  return has_owner == TRUE;
}

bool ScreenCapturerWayland::ConnectSessionBus() {
  if (dbus_connection_) {
    return true;
  }

  DBusError error;
  dbus_error_init(&error);

  dbus_connection_ = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
  if (!dbus_connection_) {
    LogDbusError("dbus_bus_get_private", &error);
    dbus_error_free(&error);
    return false;
  }

  dbus_connection_set_exit_on_disconnect(dbus_connection_, FALSE);
  return true;
}

bool ScreenCapturerWayland::CreatePortalSession() {
  if (!dbus_connection_) {
    return false;
  }

  const std::string session_handle_token = MakeToken("crossdesk_session");
  std::string request_path;
  const bool ok = SendPortalRequestAndHandleResponse(
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
                              MakeToken("crossdesk_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      running_,
      [&](uint32_t response_code, DBusMessageIter* results) {
        if (response_code != 0) {
          LOG_ERROR("CreateSession was denied or malformed, response={}",
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
                session_handle_ = parsed_handle;
                break;
              }
            }
          }
          dbus_message_iter_next(&dict);
        }
        return true;
      },
      &request_path);
  if (!ok) {
    return false;
  }

  if (session_handle_.empty()) {
    const std::string fallback_handle =
        BuildSessionHandleFromRequestPath(request_path, session_handle_token);
    if (!fallback_handle.empty()) {
      LOG_WARN(
          "CreateSession response missing session_handle, using derived handle "
          "{}",
          fallback_handle);
      session_handle_ = fallback_handle;
    }
  }

  if (session_handle_.empty()) {
    LOG_ERROR("CreateSession response did not include a session handle");
    return false;
  }

  return true;
}

bool ScreenCapturerWayland::SelectPortalSource() {
  if (!dbus_connection_ || session_handle_.empty()) {
    return false;
  }

  const char* session_handle = session_handle_.c_str();
  return SendPortalRequestAndHandleResponse(
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
        AppendDictEntryUint32(
            &options, "cursor_mode",
            show_cursor_ ? kCursorModeEmbedded : kCursorModeHidden);
        AppendDictEntryString(&options, "handle_token",
                              MakeToken("crossdesk_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      running_,
      [](uint32_t response_code, DBusMessageIter*) {
        if (response_code != 0) {
          LOG_ERROR("SelectSources was denied or malformed, response={}",
                    response_code);
          return false;
        }
        return true;
      });
}

bool ScreenCapturerWayland::SelectPortalDevices() {
  if (!dbus_connection_ || session_handle_.empty()) {
    return false;
  }

  const char* session_handle = session_handle_.c_str();
  return SendPortalRequestAndHandleResponse(
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
                              MakeToken("crossdesk_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      running_,
      [](uint32_t response_code, DBusMessageIter*) {
        if (response_code != 0) {
          LOG_ERROR("SelectDevices was denied or malformed, response={}",
                    response_code);
          return false;
        }
        return true;
      });
}

bool ScreenCapturerWayland::StartPortalSession() {
  if (!dbus_connection_ || session_handle_.empty()) {
    return false;
  }

  const char* session_handle = session_handle_.c_str();
  const char* parent_window = "";
  pointer_granted_ = false;
  const bool ok = SendPortalRequestAndHandleResponse(
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
                              MakeToken("crossdesk_req"));
        dbus_message_iter_close_container(&iter, &options);
        return true;
      },
      running_,
      [&](uint32_t response_code, DBusMessageIter* results) {
        if (response_code != 0) {
          LOG_ERROR("Start was denied or malformed, response={}",
                    response_code);
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
                int granted_devices_int = 0;
                if (ReadIntLike(&variant, &granted_devices_int) &&
                    granted_devices_int >= 0) {
                  granted_devices = static_cast<uint32_t>(granted_devices_int);
                }
              } else if (strcmp(key, "streams") == 0) {
                DBusMessageIter streams;
                dbus_message_iter_recurse(&variant, &streams);

                if (dbus_message_iter_get_arg_type(&streams) ==
                    DBUS_TYPE_STRUCT) {
                  DBusMessageIter stream;
                  dbus_message_iter_recurse(&streams, &stream);

                  if (dbus_message_iter_get_arg_type(&stream) ==
                      DBUS_TYPE_UINT32) {
                    dbus_message_iter_get_basic(&stream, &pipewire_node_id_);
                  }

                  if (dbus_message_iter_next(&stream) &&
                      dbus_message_iter_get_arg_type(&stream) ==
                          DBUS_TYPE_ARRAY) {
                    DBusMessageIter props;
                    int stream_width = 0;
                    int stream_height = 0;
                    int logical_width = 0;
                    int logical_height = 0;
                    dbus_message_iter_recurse(&stream, &props);
                    while (dbus_message_iter_get_arg_type(&props) !=
                           DBUS_TYPE_INVALID) {
                      if (dbus_message_iter_get_arg_type(&props) ==
                          DBUS_TYPE_DICT_ENTRY) {
                        DBusMessageIter prop_entry;
                        dbus_message_iter_recurse(&props, &prop_entry);

                        const char* prop_key = nullptr;
                        dbus_message_iter_get_basic(&prop_entry, &prop_key);
                        if (prop_key && dbus_message_iter_next(&prop_entry) &&
                            dbus_message_iter_get_arg_type(&prop_entry) ==
                                DBUS_TYPE_VARIANT) {
                          DBusMessageIter prop_variant;
                          dbus_message_iter_recurse(&prop_entry, &prop_variant);
                          if (dbus_message_iter_get_arg_type(&prop_variant) ==
                              DBUS_TYPE_STRUCT) {
                            DBusMessageIter size_iter;
                            int width = 0;
                            int height = 0;
                            dbus_message_iter_recurse(&prop_variant,
                                                      &size_iter);
                            if (ReadIntLike(&size_iter, &width) &&
                                dbus_message_iter_next(&size_iter) &&
                                ReadIntLike(&size_iter, &height)) {
                              if (strcmp(prop_key, "logical_size") == 0) {
                                logical_width = width;
                                logical_height = height;
                              } else if (strcmp(prop_key, "size") == 0) {
                                stream_width = width;
                                stream_height = height;
                              }
                            }
                          }
                        }
                      }
                      dbus_message_iter_next(&props);
                    }

                    const int picked_width =
                        logical_width > 0 ? logical_width : stream_width;
                    const int picked_height =
                        logical_height > 0 ? logical_height : stream_height;
                    LOG_INFO(
                        "Wayland portal stream geometry: stream_size={}x{}, "
                        "logical_size={}x{}, pointer_space={}x{}",
                        stream_width, stream_height, logical_width,
                        logical_height, picked_width, picked_height);

                    portal_stream_width_ = stream_width;
                    portal_stream_height_ = stream_height;
                    portal_has_logical_size_ =
                        logical_width > 0 && logical_height > 0;

                    if (logical_width > 0 && logical_height > 0) {
                      logical_width_ = logical_width;
                      logical_height_ = logical_height;
                      UpdateDisplayGeometry(logical_width_, logical_height_);
                    } else if (stream_width > 0 && stream_height > 0) {
                      logical_width_ = stream_width;
                      logical_height_ = stream_height;
                      UpdateDisplayGeometry(logical_width_, logical_height_);
                    }
                  }
                }
              }
            }
          }

          dbus_message_iter_next(&dict);
        }
        pointer_granted_ = (granted_devices & kRemoteDesktopDevicePointer) != 0;
        return true;
      });
  if (!ok) {
    return false;
  }

  if (pipewire_node_id_ == 0) {
    LOG_ERROR("Start response did not include a PipeWire node id");
    return false;
  }
  if (!pointer_granted_) {
    LOG_ERROR("Start response did not grant pointer control");
    return false;
  }

  shared_session_registered_ =
      PublishSharedWaylandPortalSession(SharedWaylandPortalSessionInfo{
          dbus_connection_, session_handle_, pipewire_node_id_, logical_width_,
          logical_height_, pointer_granted_});
  if (!shared_session_registered_) {
    LOG_WARN("Failed to publish shared Wayland portal session");
  }

  LOG_INFO("Wayland screencast ready, node_id={}", pipewire_node_id_);
  return true;
}

bool ScreenCapturerWayland::OpenPipeWireRemote() {
  if (!dbus_connection_ || session_handle_.empty()) {
    return false;
  }

  DBusMessage* message = dbus_message_new_method_call(
      kPortalBusName, kPortalObjectPath, kPortalScreenCastInterface,
      "OpenPipeWireRemote");
  if (!message) {
    LOG_ERROR("Failed to allocate OpenPipeWireRemote message");
    return false;
  }

  DBusMessageIter iter;
  DBusMessageIter options;
  const char* session_handle = session_handle_.c_str();
  dbus_message_iter_init_append(message, &iter);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &session_handle);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &options);
  dbus_message_iter_close_container(&iter, &options);

  DBusError error;
  dbus_error_init(&error);
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(
      dbus_connection_, message, -1, &error);
  dbus_message_unref(message);
  if (!reply) {
    LogDbusError("OpenPipeWireRemote", &error);
    dbus_error_free(&error);
    return false;
  }

  DBusMessageIter reply_iter;
  if (!dbus_message_iter_init(reply, &reply_iter) ||
      dbus_message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_UNIX_FD) {
    LOG_ERROR("OpenPipeWireRemote returned an unexpected payload");
    dbus_message_unref(reply);
    return false;
  }

  int received_fd = -1;
  dbus_message_iter_get_basic(&reply_iter, &received_fd);
  dbus_message_unref(reply);

  if (received_fd < 0) {
    LOG_ERROR("OpenPipeWireRemote returned an invalid fd");
    return false;
  }

  pipewire_fd_ = dup(received_fd);
  if (pipewire_fd_ < 0) {
    LOG_ERROR("Failed to duplicate PipeWire remote fd");
    return false;
  }

  return true;
}

void ScreenCapturerWayland::CleanupDbus() {
  if (!dbus_connection_) {
    return;
  }

  if (shared_session_registered_) {
    return;
  }

  dbus_connection_close(dbus_connection_);
  dbus_connection_unref(dbus_connection_);
  dbus_connection_ = nullptr;
}

void ScreenCapturerWayland::ClosePortalSession() {
  if (shared_session_registered_) {
    DBusConnection* close_connection = nullptr;
    std::string close_session_handle;
    ReleaseSharedWaylandPortalSession(&close_connection, &close_session_handle);
    shared_session_registered_ = false;
    if (close_connection) {
      CloseWaylandPortalSessionAndConnection(
          close_connection, close_session_handle, "Session.Close");
    }
    dbus_connection_ = nullptr;
  } else if (dbus_connection_ && !session_handle_.empty()) {
    CloseWaylandPortalSessionAndConnection(dbus_connection_, session_handle_,
                                           "Session.Close");
    dbus_connection_ = nullptr;
  }

  session_handle_.clear();
  pipewire_node_id_ = 0;
  UpdateDisplayGeometry(
      logical_width_ > 0 ? logical_width_ : kFallbackWidth,
      logical_height_ > 0 ? logical_height_ : kFallbackHeight);
  pointer_granted_ = false;
}

}  // namespace crossdesk

#endif
