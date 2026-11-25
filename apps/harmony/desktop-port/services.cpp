#include "bridge.h"
#include "displays.h"
#include "SDL3/SDL.h"
#include "audio.h"
#include "platform/path_backend.h"
#include "platform/autostart_backend.h"
#include "platform/harmony/harmony_autostart.h"
#include "platform.h"
#include <multimodalinput/oh_input_manager.h>
#include <chrono>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

using namespace crossdesk::harmony::desktop;
namespace {
std::mutex event_mutex;
std::deque<SDL_Event> events;
thread_local std::string service_error;
std::atomic<crossdesk::HarmonyAutostartStatus> autostart_status{crossdesk::HarmonyAutostartStatus::checking};
bool SetKeepScreenOn(bool active) {
  const auto result = Request({{"op", "keepScreenOn"}, {"active", active}});
  if (result.value("ok", false)) return true;
  service_error = result.value("message", "Could not change the Harmony window power setting");
  return false;
}
bool AudioFormat(const SDL_AudioSpec* spec) {
  return spec && spec->freq == 48000 && spec->channels == 1 && spec->format == SDL_AUDIO_S16;
}
}
bool SDL_Init(uint32_t) { return true; }
bool SDL_InitSubSystem(uint32_t) { return true; }
void SDL_Quit() { std::lock_guard lock(event_mutex); events.clear(); }
void SDL_QuitSubSystem(uint32_t) {}
const char* SDL_GetError() { return service_error.c_str(); }
uint64_t SDL_GetTicks() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}
void SDL_Delay(uint32_t delay) { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); }
bool SDL_PushEvent(SDL_Event* event) {
  if (!event) return false;
  std::lock_guard lock(event_mutex); events.push_back(*event); return true;
}
bool SDL_PollEvent(SDL_Event* event) { return SDL_PeepEvents(event, 1, SDL_GETEVENT, 0, UINT32_MAX) == 1; }
int SDL_PeepEvents(SDL_Event* out, int count, int, uint32_t first, uint32_t last) {
  std::lock_guard lock(event_mutex); int copied = 0;
  for (auto it = events.begin(); it != events.end() && copied < count;) {
    if (it->type >= first && it->type <= last) { out[copied++] = *it; it = events.erase(it); }
    else ++it;
  }
  return copied;
}
char* SDL_GetClipboardText() {
  auto result = Request({{"op", "clipboardRead"}});
  if (!result.value("ok", false)) {
    service_error = result.value("message", "Harmony clipboard read failed");
    return nullptr;
  }
  return strdup(result.value("text", "").c_str());
}
bool SDL_SetClipboardText(const char* text) {
  auto result = Request({{"op", "clipboardWrite"}, {"text", text ? text : ""}});
  if (result.value("ok", false)) return true;
  service_error = result.value("message", "Harmony clipboard write failed");
  return false;
}
bool SDL_HasClipboardText() { return Request({{"op", "clipboardHas"}}).value("ok", false); }
const char* SDL_GetCurrentVideoDriver() { return "harmony"; }
const SDL_DisplayMode* SDL_GetCurrentDisplayMode(SDL_DisplayID id) {
  static thread_local SDL_DisplayMode mode;
  for (const auto& d : ReadDisplays(Environment())) if (d.SdlId() == id) {
    mode = {d.width, d.height}; return &mode;
  }
  return nullptr;
}
bool SDL_EnableScreenSaver() { return SetKeepScreenOn(false); }
bool SDL_DisableScreenSaver() { return SetKeepScreenOn(true); }
float SDL_GetDisplayContentScale(SDL_DisplayID id) {
  for (const auto& display : ReadDisplays(Environment())) {
    if (display.SdlId() == id) return display.density;
  }
  return 0.0f;
}

SDL_DisplayID SDL_GetPrimaryDisplay() {
  const auto displays = ReadDisplays(Environment());
  return displays.empty() ? 0 : displays.front().SdlId();
}
SDL_DisplayID SDL_GetDisplayForPoint(const SDL_Point* point) {
  if (!point) return 0;
  const auto displays = ReadDisplays(Environment());
  const auto* found = DisplayAt(displays, point->x, point->y);
  return found ? found->SdlId() : 0;
}
SDL_DisplayID* SDL_GetDisplays(int* count) {
  const auto displays = ReadDisplays(Environment());
  auto* ids = static_cast<SDL_DisplayID*>(std::calloc(displays.size() + 1, sizeof(SDL_DisplayID)));
  if (count) *count = ids ? int(displays.size()) : 0;
  if (ids) for (size_t i = 0; i < displays.size(); ++i) ids[i] = displays[i].SdlId();
  return ids;
}
bool SDL_GetDisplayUsableBounds(SDL_DisplayID id, SDL_Rect* bounds) {
  if (!bounds) return false;
  for (const auto& d : ReadDisplays(Environment())) if (d.SdlId() == id) {
    *bounds = {d.usable_x, d.usable_y, d.usable_width, d.usable_height}; return true;
  }
  return false;
}
uint32_t SDL_GetGlobalMouseState(float* x, float* y) {
  auto env = Environment();
  double px = env.value("pointerX", 0.), py = env.value("pointerY", 0.);
  // Touch-only devices and unfocused apps may not expose a global pointer.
  // Keep the last window event coordinates intact and retry after hot-plug or
  // focus changes without issuing a failing system call every render frame.
  static thread_local std::chrono::steady_clock::time_point retry_after;
  const auto now = std::chrono::steady_clock::now();
  if (now >= retry_after) {
    int32_t display = 0;
    double native_x = px, native_y = py;
    if (OH_Input_GetPointerLocation(&display, &native_x, &native_y) == INPUT_SUCCESS) {
      px = native_x; py = native_y;
      for (const auto& d : ReadDisplays(env)) if (d.id == display) { px += d.x; py += d.y; break; }
    } else retry_after = now + std::chrono::seconds(1);
  }
  if (x) *x = px;
  if (y) *y = py;
  return env.value("buttons", 0u);
}
bool SDL_OpenURL(const char* url) { return Request({{"op", "openUrl"}, {"url", url}}).value("ok", false); }
struct SDL_AudioStream { crossdesk::harmony::AudioPlayer player; };
uint32_t SDL_OpenAudioDevice(uint32_t, const SDL_AudioSpec* spec) {
  if (AudioFormat(spec)) return 1;
  service_error = "Harmony playback requires 48 kHz mono PCM16"; return 0;
}
bool SDL_ResumeAudioDevice(uint32_t id) { return id == 1; }
void SDL_CloseAudioDevice(uint32_t) {}
SDL_AudioStream* SDL_CreateAudioStream(const SDL_AudioSpec* in, const SDL_AudioSpec* out) {
  if (!AudioFormat(in) || !AudioFormat(out)) return nullptr;
  return new SDL_AudioStream();
}
void SDL_DestroyAudioStream(SDL_AudioStream* stream) { delete stream; }
bool SDL_BindAudioStream(uint32_t id, SDL_AudioStream* stream) {
  if (id == 1 && stream && stream->player.Start()) return true;
  service_error = "OH_AudioRenderer initialization failed"; return false;
}
bool SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* data, int size) {
  if (!stream || !data || size <= 0) return false;
  stream->player.Push(static_cast<const char*>(data), size); return true;
}
const char* tinyfd_selectFolderDialog(const char* title, const char* path) {
  static thread_local std::string result;
  result = Request({{"op", "selectFolder"}, {"title", title ? title : ""},
                    {"path", path ? path : ""}}).value("path", "");
  return result.empty() ? nullptr : result.c_str();
}
const char* tinyfd_openFileDialog(const char* title, const char*, int, const char* const*, const char*, int) {
  static thread_local std::string result;
  result = Request({{"op", "selectFile"}, {"title", title ? title : ""}}).value("path", "");
  return result.empty() ? nullptr : result.c_str();
}
namespace crossdesk {
HarmonyAutostartStatus GetHarmonyAutostartStatus() { return autostart_status.load(); }
void SetHarmonyAutostartStatus(HarmonyAutostartStatus status) { autostart_status = status; }
void RefreshHarmonyAutostartStatus() {
  autostart_status = HarmonyAutostartStatus::checking;
  Emit({{"op", "autostartRefresh"}});
}
std::string GetHostName() { return Environment().value("name", "Harmony PC"); }
std::string GetMac() { return {}; } // App sandbox does not expose a hardware MAC.
bool IsWaylandSession() { return false; }
namespace platform {
std::filesystem::path GetExecutableDirectory() { return Environment().value("filesDir", ""); }
std::filesystem::path GetConfigPath(const std::string&) { return GetCachePath(""); }
std::filesystem::path GetCachePath(const std::string&) {
  auto path = GetExecutableDirectory() / "desktop";
  std::filesystem::create_directories(path); return path;
}
std::filesystem::path GetLogPath(const std::string&) {
  auto path = GetCachePath("") / "logs";
  std::filesystem::create_directories(path); return path;
}
std::string GetAutostartExecutablePath() { return {}; }
bool IsAutostartEnabled(const std::string&) {
  return GetHarmonyAutostartStatus() == HarmonyAutostartStatus::enabled;
}
// The public Harmony API can query this setting, but only the user can change
// it in system settings. Never report a registration change we did not make.
bool EnableAutostart(const std::string& app, const std::string&) { return IsAutostartEnabled(app); }
bool DisableAutostart(const std::string& app) {
  return GetHarmonyAutostartStatus() == HarmonyAutostartStatus::disabled;
}
}
}
