#include <napi/native_api.h>
#include "mobile_session.h"
#include <string>

namespace {
napi_value TrustKey(napi_env env, napi_callback_info) {
  try {
    const auto seed = crossdesk::harmony::MobileGenerateTrustKey();
    napi_value result;
    napi_create_string_utf8(env, seed.data(), seed.size(), &result);
    return result;
  } catch (const std::exception&) {
    napi_throw_error(env, nullptr, "Device signing identity unavailable"); return nullptr;
  }
}
napi_value TrustInfo(napi_env env, napi_callback_info info) {
  size_t count = 2; napi_value arguments[2];
  if (napi_get_cb_info(env, info, &count, arguments, nullptr, nullptr) != napi_ok || count != 2) {
    napi_throw_type_error(env, nullptr, "Expected device identity and public ID"); return nullptr;
  }
  std::string values[2];
  for (size_t i = 0; i < 2; ++i) {
    size_t size = 0;
    if (napi_get_value_string_utf8(env, arguments[i], nullptr, 0, &size) != napi_ok || size > 128) {
      napi_throw_type_error(env, nullptr, "Invalid device identity"); return nullptr;
    }
    values[i].resize(size + 1);
    napi_get_value_string_utf8(env, arguments[i], values[i].data(), values[i].size(), &size);
    values[i].resize(size);
  }
  try {
    const auto info = crossdesk::harmony::MobileTrustInfo(values[0], values[1]);
    napi_value result;
    napi_create_string_utf8(env, info.data(), info.size(), &result);
    return result;
  } catch (const std::exception&) {
    napi_throw_error(env, nullptr, "Device signing identity unavailable"); return nullptr;
  }
}
napi_value Command(napi_env env, napi_callback_info info) {
  size_t count = 1; napi_value argument;
  if (napi_get_cb_info(env, info, &count, &argument, nullptr, nullptr) != napi_ok || count != 1) {
    napi_throw_type_error(env, nullptr, "Expected a JSON command string"); return nullptr;
  }
  size_t size = 0;
  if (napi_get_value_string_utf8(env, argument, nullptr, 0, &size) != napi_ok || size > 1024 * 1024) {
    napi_throw_type_error(env, nullptr, "Invalid command"); return nullptr;
  }
  std::string value(size + 1, '\0');
  napi_get_value_string_utf8(env, argument, value.data(), value.size(), &size); value.resize(size);
  crossdesk::harmony::MobileCommand(value);
  napi_value result; napi_get_undefined(env, &result); return result;
}
napi_value Poll(napi_env env, napi_callback_info) {
  const std::string data = crossdesk::harmony::MobilePoll();
  napi_value result;
  napi_create_string_utf8(env, data.data(), data.size(), &result); return result;
}
napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor properties[] = {
    {"mobileGenerateTrustKey", nullptr, TrustKey, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"mobileTrustInfo", nullptr, TrustInfo, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"mobileCommand", nullptr, Command, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"mobilePoll", nullptr, Poll, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]), properties);
  napi_add_env_cleanup_hook(env, [](void*) {
    crossdesk::harmony::MobileShutdown();
  }, nullptr);
  return exports;
}
napi_module module = {1, 0, nullptr, Init, "crossdesk", nullptr, {0}};
}
extern "C" __attribute__((constructor)) void RegisterCrossDesk() { napi_module_register(&module); }
