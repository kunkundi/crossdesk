// Run with an isolated X server, for example:
// xvfb-run -a -s "-screen 0 800x600x24" xmake run linux_x11_capture_recovery_test
// Exercise the real capture loop, XGetImage and conversion. Script only the
// display topology/notifications and intercept input so no input is injected.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/XTest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mock {
struct Output {
  RROutput id;
  std::string name;
  int x, y, width, height;
};
std::mutex mutex;
std::vector<Output> outputs;
thread_local std::vector<Output> queried_outputs;
int root_width = 800, root_height = 600;
bool query_fails = false;
std::atomic<bool> event{false};
std::atomic<int> queries{0}, warnings{0}, frames{0}, recovered{0};
std::mutex frame_mutex;
int frame_width = 0, frame_height = 0;
std::string frame_stream;
int motions = 0, mouse_x = -1, mouse_y = -1, button_ups = 0;

void Expect(bool value, const char* message) {
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
template <class F>
void Wait(F predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  while (!predicate() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  Expect(predicate(), message);
}
void Set(std::vector<Output> next, bool notify = true, int width = 800,
         int height = 600, bool fail = false) {
  std::lock_guard lock(mutex);
  outputs = std::move(next);
  root_width = width;
  root_height = height;
  query_fails = fail;
  if (notify) event = true;
}
Bool QueryExtension(Display*, int* base, int* error) {
  *base = 100;
  *error = 0;
  return True;
}
void SelectInput(Display*, Window, int) {}
int Pending(Display*) { return event ? 1 : 0; }
int NextEvent(Display*, XEvent* next) {
  *next = {};
  next->type = 100 + RRNotify;
  event = false;
  return 0;
}
int UpdateConfiguration(XEvent*) { return 0; }
Status Attributes(Display* display, Window window, XWindowAttributes* attr) {
  const auto result = XGetWindowAttributes(display, window, attr);
  std::lock_guard lock(mutex);
  attr->width = root_width;
  attr->height = root_height;
  return result;
}
XRRScreenResources* Resources(Display*, Window) {
  std::lock_guard lock(mutex);
  ++queries;
  if (query_fails) return nullptr;
  queried_outputs = outputs;
  auto* result = new XRRScreenResources{};
  result->noutput = queried_outputs.size();
  result->outputs = new RROutput[result->noutput];
  for (int i = 0; i < result->noutput; ++i)
    result->outputs[i] = queried_outputs[i].id;
  return result;
}
void FreeResources(XRRScreenResources* resources) {
  delete[] resources->outputs;
  delete resources;
}
RROutput Primary(Display*, Window) { return 1; }
XRROutputInfo* OutputInfo(Display*, XRRScreenResources*, RROutput id) {
  for (auto& output : queried_outputs) {
    if (output.id != id) continue;
    auto* info = new XRROutputInfo{};
    info->connection = RR_Connected;
    info->crtc = id;
    info->name = output.name.data();
    info->nameLen = output.name.size();
    return info;
  }
  return nullptr;
}
void FreeOutput(XRROutputInfo* info) { delete info; }
XRRCrtcInfo* CrtcInfo(Display*, XRRScreenResources*, RRCrtc id) {
  for (const auto& output : queried_outputs) {
    if (output.id != id) continue;
    auto* info = new XRRCrtcInfo{};
    info->mode = output.width > 0 && output.height > 0 ? 1 : 0;
    info->x = output.x;
    info->y = output.y;
    info->width = output.width;
    info->height = output.height;
    return info;
  }
  return nullptr;
}
void FreeCrtc(XRRCrtcInfo* info) { delete info; }
XImage* GetImage(Display* display, Drawable drawable, int x, int y,
                 unsigned width, unsigned height, unsigned long planes,
                 int format) {
  {
    std::lock_guard lock(mutex);
    if (x < 0 || y < 0 || x + width > unsigned(root_width) ||
        y + height > unsigned(root_height)) return nullptr;
  }
  return XGetImage(display, drawable, x, y, width, height, planes, format);
}
template <class... Args>
void Info(const char* message, Args&&...) {
  if (std::string(message).find("capture recovered") != std::string::npos)
    ++recovered;
}
int Motion(Display*, int, int x, int y, unsigned long) {
  ++motions;
  mouse_x = x;
  mouse_y = y;
  return 1;
}
int Button(Display*, unsigned, Bool down, unsigned long) {
  if (!down) ++button_ups;
  return 1;
}
}  // namespace mock

#define XRRQueryExtension mock::QueryExtension
#define XRRSelectInput mock::SelectInput
#define XPending mock::Pending
#define XNextEvent mock::NextEvent
#define XRRUpdateConfiguration mock::UpdateConfiguration
#define XGetWindowAttributes mock::Attributes
#define XRRGetScreenResources mock::Resources
#define XRRFreeScreenResources mock::FreeResources
#define XRRGetOutputPrimary mock::Primary
#define XRRGetOutputInfo mock::OutputInfo
#define XRRFreeOutputInfo mock::FreeOutput
#define XRRGetCrtcInfo mock::CrtcInfo
#define XRRFreeCrtcInfo mock::FreeCrtc
#define XGetImage mock::GetImage
#define XTestFakeMotionEvent mock::Motion
#define XTestFakeButtonEvent mock::Button
#define _RD_LOG_H_
#define LOG_INFO(...) mock::Info(__VA_ARGS__)
#define LOG_WARN(...) (++mock::warnings)
#define LOG_ERROR(...) (++mock::warnings)
#include "../src/platform/linux/screen_capturer/screen_capturer_x11.cpp"
#include "../src/platform/linux/input/mouse/mouse_controller.cpp"

namespace crossdesk {
bool IsWaylandSession() { return false; }
bool PlatformMouseController::InitWaylandPortal() { return false; }
void PlatformMouseController::CleanupWaylandPortal() {}
int PlatformMouseController::SendWaylandMouseCommand(const Mouse&, int, bool) {
  std::abort();
}
void PlatformMouseController::OnWaylandDisplayInfoListUpdated() { std::abort(); }
}  // namespace crossdesk

namespace {
using namespace mock;
using namespace crossdesk;

void Start(ScreenCapturerX11& capturer) {
  Expect(capturer.Init(60, [](unsigned char* data, int size, int width, int height,
                             const char* stream, const MiniRtcNativeVideoFrame*) {
    Expect(data && size == width * height * 3 / 2, "valid NV12 frame");
    std::lock_guard lock(frame_mutex);
    frame_width = width;
    frame_height = height;
    frame_stream = stream;
    ++frames;
  }) == 0, "X11 initialization (run under xvfb-run with an 800x600 screen)");
  Expect(capturer.Start(false) == 0, "capture starts");
}
void Frame(int width, int height, const char* stream = "Display1") {
  const int before = frames;
  Wait([&] {
    std::lock_guard lock(frame_mutex);
    return frames > before && frame_width == width && frame_height == height &&
           frame_stream == stream;
  }, "fresh frame with the expected geometry and stream identity");
}
void Move(PlatformMouseController& mouse, int display, int expected) {
  RemoteAction action{};
  action.type = ControlType::mouse;
  action.m.flag = MouseFlag::move;
  action.m.x = action.m.y = 0.5;
  Expect(mouse.SendMouseCommand(action, display) == expected, "mouse move result");
}
}  // namespace

int main() {
  Expect(XInitThreads() != 0, "Xlib thread support");
  {
    Set({{1, "DP-2", 0, 0, 640, 480}});
    ScreenCapturerX11 capturer;
    Start(capturer);
    Frame(640, 480);
    const auto original = capturer.GetDisplayInfoList()[0];

    // The output remains enumerated, but its CRTC has no active mode.
    Set({{1, "DP-2", 0, 0, 0, 0}});
    Frame(800, 600);
    const auto fallback = capturer.GetDisplayInfoList();
    Expect(fallback.size() == 1 && fallback[0].handle == original.handle &&
           fallback[0].name == original.name, "root fallback preserves identity");
    PlatformMouseController mouse;
    Expect(mouse.Init(fallback) == 0, "mouse initializes");
    Move(mouse, 0, 0);
    Expect(mouse_x == 400 && mouse_y == 300, "fallback retains usable mouse geometry");

    // No second RandR notification when the sleeping output becomes usable.
    Set({{1, "DP-2", 0, 0, 600, 400}}, false);
    Frame(600, 400);
    mouse.UpdateDisplayInfoList(capturer.GetDisplayInfoList());
    Move(mouse, 0, 0);
    Expect(mouse_x == 300 && mouse_y == 200, "wake restores mouse geometry");

    // A lost resize event leaves a rectangle larger than the current root.
    const int recovered_before = recovered;
    Set({{1, "DP-2", 0, 0, 320, 240}}, false, 320, 240);
    Frame(320, 240);
    Expect(recovered > recovered_before, "XGetImage failure triggers recovery");

    // A transient query error must not be mistaken for disconnected outputs.
    const int query_before = queries;
    Set({}, true, 800, 600, true);
    Wait([&] { return queries > query_before; }, "failed query attempted");
    Expect(capturer.GetDisplayInfoList()[0].width == 320,
           "query failure retains previous geometry");
    Set({{1, "DP-2", 0, 0, 480, 360}}, false);
    Frame(480, 360);

    capturer.Stop();
    Set({{1, "DP-2", 0, 0, 640, 480}}, false);
    Expect(capturer.Start(false) == 0, "capture restarts");
    Frame(640, 480);
  }
  {
    Set({{1, "DP-1", 0, 0, 400, 600}, {2, "DP-2", 400, 0, 400, 600}});
    ScreenCapturerX11 capturer;
    Start(capturer);
    Expect(capturer.SwitchTo(1) == 0, "select second stream");
    Frame(400, 600, "Display2");
    Set({{1, "DP-1", 0, 0, 400, 600}});
    Wait([&] { return capturer.GetDisplayInfoList()[1].width == 0; },
         "missing second output is unavailable");
    const auto missing = capturer.GetDisplayInfoList();
    Expect(missing.size() == 2 && missing[0].width == 400,
           "disconnect preserves the other stream and slot count");
    const int frames_before = frames, warnings_before = warnings;
    const int queries_before = queries;
    Wait([&] { return queries >= queries_before + 2; }, "reprobe without events");
    Expect(frames == frames_before, "missing stream does not capture another screen");
    Expect(warnings - warnings_before <= 1, "invalid geometry logs are throttled");

    PlatformMouseController mouse;
    Expect(mouse.Init(missing) == 0, "mouse initializes with unavailable output");
    const int motions_before = motions;
    Move(mouse, 1, -2);
    Expect(motions == motions_before, "zero geometry does not inject motion");
    RemoteAction action{};
    action.type = ControlType::mouse;
    action.m.flag = MouseFlag::left_up;
    const int ups_before = button_ups;
    Expect(mouse.SendMouseCommand(action, 1) == 0 && button_ups > ups_before,
           "button releases remain possible with unavailable geometry");

    capturer.Pause(1);
    const int paused_frames = frames;
    Set({{1, "DP-1", 0, 0, 300, 600}, {2, "DP-2", 300, 0, 500, 600}}, false);
    Wait([&] { return capturer.GetDisplayInfoList()[1].width == 500; },
         "recovery continues while paused without a RandR event");
    Expect(frames == paused_frames, "paused recovery does not send video");
    capturer.Resume(1);
    Frame(500, 600, "Display2");
  }
  std::cout << "X11 fallback, wake/resize recovery, retry, stream identity and mouse geometry passed\n";
}
