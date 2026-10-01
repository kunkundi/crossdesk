#include <jni.h>
#include <android/native_window_jni.h>
#include <libyuv.h>
#include <minirtc.h>
#include <log.h>
#include <remote_action.h>
#include <app_version.h>
#include <stream_names.h>
#include <display_stream_id.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <stdexcept>
#include <vector>
#include "controller_protocol.h"
#include "android_codec_support.h"
#include "latest_frame_worker.h"
#include "video_picture.h"
#include "video_timing.h"

namespace {
using namespace crossdesk;
using nlohmann::json;
JavaVM* vm = nullptr;
struct Environment {
  JNIEnv* env = nullptr;
  bool attached = false;
  Environment() {
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
      attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
    }
  }
  ~Environment() { if (attached) vm->DetachCurrentThread(); }
};
std::string String(JNIEnv* env, jstring value) {
  if (!value) return {};
  const char* bytes = env->GetStringUTFChars(value, nullptr);
  std::string result = bytes ? bytes : "";
  if (bytes) env->ReleaseStringUTFChars(value, bytes);
  return result;
}
struct Session;
using android_controller::VideoPicture;
void RenderVideo(Session* s, VideoPicture picture);
struct CallbackContext { Session* owner; bool controller; };
struct Session {
  jobject owner = nullptr;
  jmethodID event_method, audio_method, frame_method;
  PeerPtr* identity = nullptr;
  PeerPtr* controller = nullptr;
  CallbackContext identity_context{this, false}, controller_context{this, true};
  std::string host, log_path, identity_login, controller_login, remote;
  int port;
  std::atomic<bool> stopping{false};
  std::atomic<int> display{0};
  std::atomic<bool> surface_ready{false};
  std::atomic<uint64_t> video_generation{0};
  std::mutex render_mutex;
  ANativeWindow* window = nullptr;
  uint32_t width = 0, height = 0;
  uint64_t frame_id = 0;
  std::optional<VideoPicture> surface_picture;
  void DetachDecoderSurface() {
    if (surface_picture) surface_picture->RenderSurface(nullptr);
    surface_picture.reset();
  }
  std::set<int> pressed_keys;
  unsigned pressed_buttons = 0;
  float pointer_x = .5f, pointer_y = .5f;
  android_controller::LatestFrameWorker<VideoPicture> renderer{
      [this](VideoPicture picture) { RenderVideo(this, std::move(picture)); }};

  void Event(int type, const char* data, size_t size) {
    if (stopping || size > 1024 * 1024) return;
    Environment scope;
    if (!scope.env) return;
    jbyteArray bytes = scope.env->NewByteArray(static_cast<jsize>(size));
    if (!bytes) return;
    scope.env->SetByteArrayRegion(bytes, 0, size, reinterpret_cast<const jbyte*>(data));
    scope.env->CallVoidMethod(owner, event_method, type, bytes);
    scope.env->DeleteLocalRef(bytes);
    if (scope.env->ExceptionCheck()) scope.env->ExceptionClear();
  }
  void Event(int type, const json& value) {
    const std::string data = value.dump(); Event(type, data.data(), data.size());
  }
  void Send(const char* stream, const std::string& data) {
    if (controller && !stopping && !data.empty())
      SendReliableDataFrame(controller, data.data(), data.size(), stream);
  }
  void Surface(JNIEnv* env, jobject surface) {
    std::lock_guard<std::mutex> lock(render_mutex);
    ++video_generation;
    renderer.Clear();
    DetachDecoderSurface();
    if (window) ANativeWindow_release(window);
    window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    surface_ready = window != nullptr;
    width = height = 0;
  }
  ~Session() {
    // API calls and destruction share the Java RTC executor. Release any
    // delivered input even when queued Java key-up events were cancelled.
    if (controller) {
      for (int key : pressed_keys) {
        RemoteAction action{}; action.type = keyboard;
        action.k = {static_cast<size_t>(key), 0, false, key_up};
        Send(kKeyboardStream, action.to_json());
      }
      for (int button = 0; button < 3; ++button) {
        if (!(pressed_buttons & (1u << button))) continue;
        RemoteAction action{}; action.type = mouse;
        action.m = {pointer_x, pointer_y, 0, static_cast<MouseFlag>(2 + button * 2)};
        Send(kMouseStream, action.to_json());
      }
    }
    stopping = true;
    renderer.Stop();
    if (controller) {
      LeaveConnection(controller, remote.c_str());
      DestroyPeer(&controller);
    }
    if (identity) DestroyPeer(&identity);
    Environment scope;
    Surface(scope.env, nullptr);
    if (scope.env) scope.env->DeleteGlobalRef(owner);
  }
};

void Signal(SignalStatus status, const char*, size_t, void* user) {
  auto* ctx = static_cast<CallbackContext*>(user);
  ctx->owner->Event(1, {{"controller", ctx->controller}, {"status", status}});
}
void SignalMessage(const char* data, size_t size, void* user) {
  auto* ctx = static_cast<CallbackContext*>(user);
  if (ctx->controller || !data || !size || size > 1024 * 1024) return;
  auto message = json::parse(data, data + size, nullptr, false);
  if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) return;
  const auto type = message["type"].get<std::string>();
  if (type == "announcements" || type == "announcements_changed") ctx->owner->Event(8, data, size);
}
void Connection(ConnectionStatus status, const char*, size_t, void* user) {
  auto* ctx = static_cast<CallbackContext*>(user);
  if (ctx->controller) ctx->owner->Event(2, {{"status", status}});
}
void Network(const char* id, size_t size, TraversalMode mode,
             const MiniRtcNetTrafficStats* stats, const char*, size_t, void* user) {
  auto* ctx = static_cast<CallbackContext*>(user);
  if (!ctx->controller && mode == UnknownMode && id && size) {
    ctx->owner->Event(3, id, size);
  } else if (ctx->controller && stats) {
    ctx->owner->Event(7, android_controller::NetworkReport(*stats, mode));
  }
}
void Data(const char* data, size_t size, const char*, size_t,
          const char* stream, size_t stream_size, void* user) {
  auto* ctx = static_cast<CallbackContext*>(user);
  if (!ctx->controller || !data || !stream || size > kMaxClipboardBytes) return;
  const std::string name(stream, stream_size);
  if (name == kControlStream) {
    RemoteAction action{};
    if (action.from_json(std::string(data, size))) {
      const auto message = action.to_json();
      ctx->owner->Event(4, message.data(), message.size());
      FreeRemoteAction(action);
    }
  }
  if (name == kClipboardStream) ctx->owner->Event(6, data, size);
}
void Audio(const char* data, size_t size, const char*, size_t,
           const char*, size_t, void* user) {
  auto* ctx = static_cast<CallbackContext*>(user);
  auto* s = ctx->owner;
  if (!ctx->controller || s->stopping || !data || !size || size > 23040) return;
  Environment scope;
  if (!scope.env) return;
  auto bytes = scope.env->NewByteArray(size);
  if (!bytes) return;
  scope.env->SetByteArrayRegion(bytes, 0, size, reinterpret_cast<const jbyte*>(data));
  scope.env->CallVoidMethod(s->owner, s->audio_method, bytes);
  scope.env->DeleteLocalRef(bytes);
  if (scope.env->ExceptionCheck()) scope.env->ExceptionClear();
}
void Video(const MiniRtcVideoFrame* frame, const char*, size_t,
           const char* stream, size_t stream_size, void* user) {
  auto* ctx = static_cast<CallbackContext*>(user);
  auto* s = ctx->owner;
  const auto generation = s->video_generation.load();
  if (!ctx->controller || s->stopping || !s->surface_ready || !frame || !stream ||
      std::string(stream, stream_size) != MakeDisplayStreamId(s->display.load())) return;
  auto picture = VideoPicture::RetainOrCopy(*frame, generation);
  if (picture) s->renderer.Submit(std::move(*picture));
}
void RenderVideo(Session* s, VideoPicture picture) {
  const auto w = picture.width, h = picture.height;
  std::lock_guard<std::mutex> lock(s->render_mutex);
  if (s->stopping || !s->window || picture.generation != s->video_generation) return;
  const bool size_changed = s->width != w || s->height != h;
  if (picture.IsSurface()) {
    if (picture.RenderSurface(s->window) != 0) return;
    // Keep a consumed token to detach the codec before Surface destruction,
    // monitor switching, or CPU rendering after a software fallback.
    s->surface_picture = picture;
  } else {
    const bool was_surface = s->surface_picture.has_value();
    s->DetachDecoderSurface();
    if ((size_changed || was_surface) &&
        ANativeWindow_setBuffersGeometry(s->window, w, h, WINDOW_FORMAT_RGBA_8888) != 0) return;
    ANativeWindow_Buffer buffer{};
    if (ANativeWindow_lock(s->window, &buffer, nullptr) != 0) return;
    const bool rendered = buffer.width == static_cast<int>(w) && buffer.height == static_cast<int>(h) &&
      libyuv::NV12ToABGR(picture.Y(), picture.YStride(), picture.UV(), picture.UVStride(),
                       static_cast<uint8_t*>(buffer.bits), buffer.stride * 4, w, h) == 0;
    if (ANativeWindow_unlockAndPost(s->window) != 0 || !rendered) return;
  }
  if (size_changed) {
    s->width = w; s->height = h;
    s->Event(5, {{"width", w}, {"height", h}});
  }
  // Measure through display submission, using MiniRTC's calibrated capture clock.
  const auto now = GetSystemTimeMicros(s->controller);
  const double latency = android_controller::VideoLatencyMilliseconds(
      picture.captured_us, picture.received_us, now);
  Environment scope;
  if (scope.env) {
    scope.env->CallVoidMethod(s->owner, s->frame_method, static_cast<jlong>(++s->frame_id), latency, static_cast<jint>(w), static_cast<jint>(h));
    if (scope.env->ExceptionCheck()) scope.env->ExceptionClear();
  }
}
Params Parameters(Session* s, bool controller) {
  Params p{};
  std::snprintf(p.signal_server_ip, sizeof(p.signal_server_ip), "%s", s->host.c_str());
  std::snprintf(p.log_path, sizeof(p.log_path), "%s", s->log_path.c_str());
  p.signal_server_port = s->port;
  p.hardware_acceleration = true;
  // MediaCodec returns retained Surface buffers; software fallback returns
  // pooled CPU NV12 with the same ownership contract.
  p.native_video_output = controller;
  p.turn_mode = TurnAutoUdpTcp;
  p.enable_srtp = true;
  p.video_content_type = VideoContentType::ScreenContent;
  p.video_quality = QualityMedium;
  p.video_frame_rate = 30;
  p.video_degradation_preference = VideoDegradationPreference::Balanced;
  p.on_signal_status = Signal; p.on_connection_status = Connection;
  p.on_signal_message = SignalMessage;
  p.on_net_status_report = Network; p.on_receive_data_buffer = Data;
  p.on_receive_video_frame = Video; p.on_receive_audio_buffer = Audio;
  p.user_id = (controller ? s->controller_login : s->identity_login).c_str();
  p.user_data = controller ? &s->controller_context : &s->identity_context;
  return p;
}
Session* From(jlong handle) { return reinterpret_cast<Session*>(handle); }
} // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* java_vm, void*) {
  vm = java_vm;
  minirtc::android::SetJavaVm(java_vm);
  return JNI_VERSION_1_6;
}
#define JNI_METHOD(name) Java_cn_crossdesk_mobile_NativeSession_##name
extern "C" JNIEXPORT jboolean JNICALL JNI_METHOD(nValidAppVersion)(
    JNIEnv* env, jclass, jstring version) {
  return version && env->GetStringUTFLength(version) < 64 &&
         crossdesk::IsValidAppVersion(String(env, version));
}
extern "C" JNIEXPORT jboolean JNICALL JNI_METHOD(nHasAppUpdate)(
    JNIEnv* env, jclass, jstring version, jbyteArray release_json) {
  if (!release_json) return false;
  const auto size = env->GetArrayLength(release_json);
  if (size <= 0 || size > 256 * 1024) return false;
  std::string data(size, '\0');
  env->GetByteArrayRegion(release_json, 0, size, reinterpret_cast<jbyte*>(data.data()));
  if (env->ExceptionCheck()) return false;
  const auto latest = crossdesk::ParseVersionInfoJSON(data);
  return latest && crossdesk::AvailableAppUpdate(String(env, version), *latest).has_value();
}
extern "C" JNIEXPORT jlong JNICALL JNI_METHOD(nCreate)(
    JNIEnv* env, jclass, jobject owner, jstring host, jint port, jstring identity,
    jstring log_path, jstring certificates) {
  try {
    auto s = std::make_unique<Session>();
    s->owner = env->NewGlobalRef(owner);
    auto cls = env->GetObjectClass(owner);
    s->event_method = env->GetMethodID(cls, "onNativeEvent", "(I[B)V");
    s->audio_method = env->GetMethodID(cls, "onNativeAudio", "([B)V");
    s->frame_method = env->GetMethodID(cls, "onNativeVideoFrame", "(JDII)V");
    env->DeleteLocalRef(cls);
    s->host = String(env, host); s->port = port;
    s->identity_login = String(env, identity); s->log_path = String(env, log_path);
    // The bundle is exported from Android's default TrustManager, never the
    // build machine's certificate store. MiniRTC on Android explicitly
    // loads this path: OpenSSL's default loader ignores SSL_CERT_FILE in an
    // Android app process (AT_SECURE=1). Chain and hostname checks stay enabled.
    const auto bundle = String(env, certificates);
    if (bundle.empty() || setenv("SSL_CERT_FILE", bundle.c_str(), 1) != 0)
      throw std::runtime_error("Unable to configure Android trust bundle");
    // MiniRTC's diagnostic log includes login identities. Keep credentials
    // solely in Keystore-backed storage; do not persist its session logging.
    minirtc::InitLogger(s->log_path);
    minirtc::get_logger()->set_level(spdlog::level::off);
    auto params = Parameters(s.get(), false);
    s->identity = CreatePeer(&params);
    if (!s->identity || Init(s->identity) != 0) return 0;
    return reinterpret_cast<jlong>(s.release());
  } catch (const std::exception&) { return 0; }
}
extern "C" JNIEXPORT jboolean JNICALL JNI_METHOD(nConnect)(
    JNIEnv* env, jclass, jlong handle, jstring identity, jstring remote, jstring password) {
  auto* s = From(handle);
  if (!s || s->controller) return false;
  s->controller_login = "C-" + String(env, identity);
  s->remote = String(env, remote);
  auto params = Parameters(s, true);
  s->controller = CreatePeer(&params);
  if (!s->controller) return false;
  AddAudioStream(s->controller, kAudioStream);
  AddDataStream(s->controller, kDataStream, false);
  for (const char* name : {kMouseStream, kKeyboardStream, kControlStream,
                          kClipboardStream}) AddDataStream(s->controller, name, true);
  if (Init(s->controller) != 0) return false;
  const auto join = s->remote + "@" + String(env, password);
  return JoinConnection(s->controller, join.c_str()) == 0;
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nReady)(JNIEnv*, jclass, jlong handle) {
  auto* s = From(handle); if (!s || !s->controller) return;
  const std::string info = R"({"type":"client_info","version":"android-native","platform":"android"})";
  SendSignalMessage(s->controller, info.data(), info.size());
  auto action = MakeHostInformation("CrossDesk Android", {}, false, "0.1.0", HostPlatform::Android);
  s->Send(kControlStream, action.to_json()); FreeRemoteAction(action);
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nPointer)(
    JNIEnv*, jclass, jlong handle, jfloat x, jfloat y, jint flag, jint wheel) {
  auto* s = From(handle); if (!s || flag < 0 || flag > 8) return;
  RemoteAction action{}; action.type = mouse;
  action.m = {std::clamp(x, 0.f, 1.f), std::clamp(y, 0.f, 1.f), wheel, static_cast<MouseFlag>(flag)};
  s->pointer_x = action.m.x; s->pointer_y = action.m.y;
  if (flag >= 1 && flag <= 6) {
    const unsigned bit = 1u << ((flag - 1) / 2);
    if (flag & 1) s->pressed_buttons |= bit; else s->pressed_buttons &= ~bit;
  }
  s->Send(kMouseStream, action.to_json());
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nKey)(JNIEnv*, jclass, jlong handle, jint key, jboolean down) {
  auto* s = From(handle); if (!s) return;
  RemoteAction action{}; action.type = keyboard;
  action.k = {static_cast<size_t>(key), 0, false, down ? key_down : key_up};
  if (down) s->pressed_keys.insert(key); else s->pressed_keys.erase(key);
  s->Send(kKeyboardStream, action.to_json());
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nControl)(
    JNIEnv*, jclass, jlong handle, jint type, jint value) {
  auto* s = From(handle); if (!s) return;
  RemoteAction action{};
  if (type == display_id && value >= 0 && value < 8) {
    { std::lock_guard<std::mutex> lock(s->render_mutex); s->display = value; ++s->video_generation; s->renderer.Clear(); s->DetachDecoderSurface(); s->width = s->height = 0; }
    action.type = display_id; action.d = value;
  } else if (type == audio_capture) {
    action.type = audio_capture; action.a = value != 0;
  } else return;
  s->Send(kControlStream, action.to_json());
}
extern "C" JNIEXPORT jboolean JNICALL JNI_METHOD(nVideoSettings)(
    JNIEnv*, jclass, jlong handle, jint quality, jint rate, jint preference, jlong request_id) {
  auto* s = From(handle);
  if (!s || !s->controller || s->stopping || request_id < 0 || request_id > UINT32_MAX) return false;
  const auto data = android_controller::VideoSettingsMessage(quality, rate, preference, static_cast<uint32_t>(request_id));
  if (data.empty()) return false;
  return SendReliableDataFrame(s->controller, data.data(), data.size(), kControlStream) == 0;
}
extern "C" JNIEXPORT jboolean JNICALL JNI_METHOD(nAnnouncementRequest)(
    JNIEnv* env, jclass, jlong handle, jbyteArray data) {
  auto* s = From(handle);
  if (!s || !s->identity || s->stopping || !data) return false;
  const auto size = env->GetArrayLength(data);
  if (size <= 0 || size > 4096) return false;
  std::string bytes(size, '\0');
  env->GetByteArrayRegion(data, 0, size, reinterpret_cast<jbyte*>(bytes.data()));
  if (env->ExceptionCheck()) return false;
  auto message = json::parse(bytes, nullptr, false);
  if (!message.is_object() || !message.contains("type") ||
      !message["type"].is_string() || message["type"] != "announcements_list") return false;
  return SendSignalMessage(s->identity, bytes.data(), bytes.size()) == 0;
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nClipboard)(
    JNIEnv* env, jclass, jlong handle, jbyteArray data) {
  auto* s = From(handle); if (!s || !data) return;
  const auto size = env->GetArrayLength(data);
  if (!size || size > static_cast<int>(kMaxClipboardBytes)) return;
  std::string bytes(size, '\0');
  env->GetByteArrayRegion(data, 0, size, reinterpret_cast<jbyte*>(bytes.data()));
  s->Send(kClipboardStream, bytes);
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nSurface)(JNIEnv* env, jclass, jlong handle, jobject surface) {
  if (auto* s = From(handle)) s->Surface(env, surface);
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(nDestroy)(JNIEnv*, jclass, jlong handle) { delete From(handle); }
