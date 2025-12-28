
#include "clipboard.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "rd_log.h"

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <cstring>
#include <memory>
#endif

namespace crossdesk {

std::atomic<bool> g_monitoring{false};
std::thread g_monitor_thread;
std::mutex g_monitor_mutex;
std::string g_last_clipboard_text;
int g_check_interval_ms = 100;
Clipboard::OnClipboardChanged g_on_clipboard_changed;

#ifdef _WIN32
HWND g_clipboard_wnd = nullptr;
const char* g_clipboard_class_name = "CrossDeskClipboardMonitor";
#endif

#ifdef __linux__
Display* g_x11_display = nullptr;
Atom g_clipboard_atom = None;
Atom g_xfixes_selection_notify = None;
#endif
}  // namespace crossdesk

namespace crossdesk {

#ifdef _WIN32
std::string Clipboard::GetText() {
  if (!OpenClipboard(nullptr)) {
    LOG_ERROR("Clipboard::GetText: failed to open clipboard");
    return "";
  }

  std::string result;
  HANDLE hData = GetClipboardData(CF_UNICODETEXT);
  if (hData != nullptr) {
    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pszText != nullptr) {
      int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, nullptr, 0,
                                            nullptr, nullptr);
      if (size_needed > 0) {
        // -1 because WideCharToMultiByte contains '\0'
        result.resize(size_needed - 1);
        WideCharToMultiByte(CP_UTF8, 0, pszText, -1, &result[0], size_needed,
                            nullptr, nullptr);
      }
      GlobalUnlock(hData);
    }
  }

  CloseClipboard();
  return result;
}

bool Clipboard::SetText(const std::string& text) {
  if (!OpenClipboard(nullptr)) {
    LOG_ERROR("Clipboard::SetText: failed to open clipboard");
    return false;
  }

  if (!EmptyClipboard()) {
    LOG_ERROR("Clipboard::SetText: failed to empty clipboard");
    CloseClipboard();
    return false;
  }

  // Convert UTF-8 string to wide char
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size_needed <= 0) {
    LOG_ERROR("Clipboard::SetText: failed to convert to wide char");
    CloseClipboard();
    return false;
  }

  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size_needed * sizeof(wchar_t));
  if (hMem == nullptr) {
    LOG_ERROR("Clipboard::SetText: failed to allocate memory");
    CloseClipboard();
    return false;
  }

  wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hMem));
  if (pszText == nullptr) {
    LOG_ERROR("Clipboard::SetText: failed to lock memory");
    GlobalFree(hMem);
    CloseClipboard();
    return false;
  }

  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pszText, size_needed);
  GlobalUnlock(hMem);

  if (SetClipboardData(CF_UNICODETEXT, hMem) == nullptr) {
    LOG_ERROR("Clipboard::SetText: failed to set clipboard data");
    GlobalFree(hMem);
    CloseClipboard();
    return false;
  }

  CloseClipboard();
  return true;
}

bool Clipboard::HasText() {
  if (!OpenClipboard(nullptr)) {
    return false;
  }

  bool has_text = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                  IsClipboardFormatAvailable(CF_TEXT);
  CloseClipboard();
  return has_text;
}

#elif __APPLE__
// macOS implementation is in clipboard_mac.mm
#elif __linux__

std::string Clipboard::GetText() {
  Display* display = XOpenDisplay(nullptr);
  if (display == nullptr) {
    LOG_ERROR("Clipboard::GetText: failed to open X display");
    return "";
  }

  std::string result;
  Window owner = XGetSelectionOwner(display, XA_PRIMARY);
  if (owner == None) {
    // Try using CLIPBOARD
    owner =
        XGetSelectionOwner(display, XInternAtom(display, "CLIPBOARD", False));
    if (owner == None) {
      XCloseDisplay(display);
      return "";
    }
  }

  Atom selection = XA_PRIMARY;
  Atom target = XInternAtom(display, "UTF8_STRING", False);
  if (target == None) {
    target = XA_STRING;
  }

  XEvent event;
  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      1, 1, 0, 0, 0);
  XSelectInput(display, window, PropertyChangeMask);

  XConvertSelection(display, selection, target, XA_PRIMARY, window,
                    CurrentTime);

  // Wait for selection conversion to complete
  bool done = false;
  while (!done) {
    XNextEvent(display, &event);
    if (event.type == SelectionNotify) {
      if (event.xselection.property == None) {
        // Try using CLIPBOARD
        if (selection == XA_PRIMARY) {
          selection = XInternAtom(display, "CLIPBOARD", False);
          XConvertSelection(display, selection, target, XA_PRIMARY, window,
                            CurrentTime);
          continue;
        }
        break;
      }

      Atom actual_type;
      int actual_format;
      unsigned long nitems;
      unsigned long bytes_after;
      unsigned char* data = nullptr;

      if (XGetWindowProperty(display, window, XA_PRIMARY, 0, LONG_MAX / 4,
                             False, AnyPropertyType, &actual_type,
                             &actual_format, &nitems, &bytes_after,
                             &data) == Success) {
        if (data != nullptr) {
          result = std::string(reinterpret_cast<char*>(data), nitems);
          XFree(data);
        }
        done = true;
      }
    }
  }

  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return result;
}

bool Clipboard::SetText(const std::string& text) {
  Display* display = XOpenDisplay(nullptr);
  if (display == nullptr) {
    LOG_ERROR("Clipboard::SetText: failed to open X display");
    return false;
  }

  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      1, 1, 0, 0, 0);
  Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
  Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
  Atom targets = XInternAtom(display, "TARGETS", False);
  Atom xa_string = XA_STRING;

  XSetSelectionOwner(display, clipboard, window, CurrentTime);
  if (XGetSelectionOwner(display, clipboard) != window) {
    LOG_ERROR("Clipboard::SetText: failed to set selection owner");
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return false;
  }

  XChangeProperty(display, window, XA_PRIMARY, utf8_string, 8, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(text.c_str()),
                  static_cast<int>(text.length()));

  XEvent event;
  while (true) {
    XNextEvent(display, &event);
    if (event.type == SelectionRequest) {
      XSelectionRequestEvent* req = &event.xselectionrequest;
      XSelectionEvent se;
      se.type = SelectionNotify;
      se.display = req->display;
      se.requestor = req->requestor;
      se.selection = req->selection;
      se.time = req->time;
      se.target = req->target;
      se.property = req->property;

      if (req->target == targets) {
        // Return supported formats
        Atom supported[] = {utf8_string, xa_string, targets};
        XChangeProperty(display, req->requestor, req->property, XA_ATOM, 32,
                        PropModeReplace,
                        reinterpret_cast<unsigned char*>(supported), 3);
        se.property = req->property;
      } else if (req->target == utf8_string || req->target == xa_string) {
        // Return text data
        XChangeProperty(display, req->requestor, req->property, req->target, 8,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char*>(text.c_str()),
                        static_cast<int>(text.length()));
        se.property = req->property;
      } else {
        se.property = None;
      }

      XSendEvent(display, req->requestor, False, 0,
                 reinterpret_cast<XEvent*>(&se));
      XSync(display, False);
    } else if (event.type == SelectionClear) {
      break;
    }
  }

  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return true;
}

bool Clipboard::HasText() {
  Display* display = XOpenDisplay(nullptr);
  if (display == nullptr) {
    return false;
  }

  Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
  Window owner = XGetSelectionOwner(display, clipboard);
  if (owner == None) {
    owner = XGetSelectionOwner(display, XA_PRIMARY);
  }

  XCloseDisplay(display);
  return owner != None;
}

#else

std::string Clipboard::GetText() {
  LOG_ERROR("Clipboard::GetText: unsupported platform");
  return "";
}

bool Clipboard::SetText(const std::string& text) {
  LOG_ERROR("Clipboard::SetText: unsupported platform");
  return false;
}

bool Clipboard::HasText() {
  LOG_ERROR("Clipboard::HasText: unsupported platform");
  return false;
}

#endif

void HandleClipboardChange() {
  if (!Clipboard::HasText()) {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    if (!g_last_clipboard_text.empty()) {
      g_last_clipboard_text.clear();
      LOG_INFO("Clipboard content cleared");
    }
    return;
  }

  std::string current_text = Clipboard::GetText();

  // Check if the content has changed
  {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    if (current_text != g_last_clipboard_text) {
      g_last_clipboard_text = current_text;
      if (!current_text.empty()) {
        if (g_on_clipboard_changed) {
          int ret = g_on_clipboard_changed(current_text.c_str(),
                                           current_text.length());
          if (ret != 0) {
            LOG_WARN("Clipboard callback returned error: {}", ret);
          }
        }
      }
    }
  }
}

#ifdef _WIN32
LRESULT CALLBACK ClipboardWndProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam) {
  if (uMsg == WM_CLIPBOARDUPDATE) {
    HandleClipboardChange();
    return 0;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void MonitorThreadFunc() {
  // Create a hidden window to receive clipboard messages
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = ClipboardWndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = g_clipboard_class_name;
  RegisterClassA(&wc);

  g_clipboard_wnd = CreateWindowA(g_clipboard_class_name, nullptr, 0, 0, 0, 0,
                                  0, HWND_MESSAGE, nullptr, nullptr, nullptr);
  if (!g_clipboard_wnd) {
    LOG_ERROR("Failed to create clipboard monitor window");
    g_monitoring.store(false);
    return;
  }

  // Register clipboard format listener
  if (!AddClipboardFormatListener(g_clipboard_wnd)) {
    LOG_ERROR("Failed to add clipboard format listener");
    DestroyWindow(g_clipboard_wnd);
    g_clipboard_wnd = nullptr;
    g_monitoring.store(false);
    return;
  }

  LOG_INFO("Clipboard event monitoring started (Windows)");

  MSG msg;
  while (g_monitoring.load()) {
    BOOL ret = GetMessage(&msg, nullptr, 0, 0);
    if (ret == 0 || ret == -1) {
      break;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  RemoveClipboardFormatListener(g_clipboard_wnd);
  if (g_clipboard_wnd) {
    DestroyWindow(g_clipboard_wnd);
    g_clipboard_wnd = nullptr;
  }
  UnregisterClassA(g_clipboard_class_name, GetModuleHandle(nullptr));
}

#elif __APPLE__
// macOS use notification mechanism, need external functions
extern void StartMacOSClipboardMonitoring();
extern void StopMacOSClipboardMonitoring();

static void MonitorThreadFunc() { StartMacOSClipboardMonitoring(); }

#elif __linux__
static void MonitorThreadFunc() {
  g_x11_display = XOpenDisplay(nullptr);
  if (!g_x11_display) {
    LOG_ERROR("Failed to open X display for clipboard monitoring");
    g_monitoring.store(false);
    return;
  }

  // Check if XFixes extension is available
  int event_base, error_base;
  if (!XFixesQueryExtension(g_x11_display, &event_base, &error_base)) {
    LOG_WARN("XFixes extension not available, falling back to polling");
    XCloseDisplay(g_x11_display);
    g_x11_display = nullptr;
    // fallback to polling mode
    while (g_monitoring.load()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(g_check_interval_ms));
      if (!g_monitoring.load()) {
        break;
      }
      HandleClipboardChange();
    }
    return;
  }

  g_clipboard_atom = XInternAtom(g_x11_display, "CLIPBOARD", False);
  g_xfixes_selection_notify =
      XInternAtom(g_x11_display, "XFIXES_SELECTION_NOTIFY", False);

  // Create event window
  Window root = DefaultRootWindow(g_x11_display);
  Window event_window =
      XCreateSimpleWindow(g_x11_display, root, 0, 0, 1, 1, 0, 0, 0);

  // Select events to monitor
  XFixesSelectSelectionInput(g_x11_display, event_window, g_clipboard_atom,
                             XFixesSetSelectionOwnerNotifyMask |
                                 XFixesSelectionWindowDestroyNotifyMask |
                                 XFixesSelectionClientCloseNotifyMask);

  LOG_INFO("Clipboard event monitoring started (Linux XFixes)");

  XEvent event;
  while (g_monitoring.load()) {
    XNextEvent(g_x11_display, &event);
    if (event.type == event_base + XFixesSelectionNotify) {
      HandleClipboardChange();
    }
  }

  XFixesSelectSelectionInput(g_x11_display, event_window, g_clipboard_atom, 0);
  XDestroyWindow(g_x11_display, event_window);
  XCloseDisplay(g_x11_display);
  g_x11_display = nullptr;
}

#else
// Fallback to polling mode for other platforms
static void MonitorThreadFunc() {
  while (g_monitoring.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(g_check_interval_ms));
    if (!g_monitoring.load()) {
      break;
    }
    HandleClipboardChange();
  }
}
#endif

void Clipboard::StartMonitoring(int check_interval_ms,
                                OnClipboardChanged on_changed) {
  if (g_monitoring.load()) {
    LOG_WARN("Clipboard monitoring is already running");
    return;
  }

  g_check_interval_ms = check_interval_ms > 0 ? check_interval_ms : 100;
  {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    g_on_clipboard_changed = on_changed;
    if (HasText()) {
      g_last_clipboard_text = GetText();
    } else {
      g_last_clipboard_text.clear();
    }
  }
  g_monitoring.store(true);

  g_monitor_thread = std::thread(MonitorThreadFunc);
  LOG_INFO("Clipboard event monitoring started");
}

void Clipboard::StopMonitoring() {
  if (!g_monitoring.load()) {
    return;
  }

  g_monitoring.store(false);

#ifdef _WIN32
  if (g_clipboard_wnd) {
    PostMessage(g_clipboard_wnd, WM_QUIT, 0, 0);
  }
#elif __APPLE__
  StopMacOSClipboardMonitoring();
#endif

  if (g_monitor_thread.joinable()) {
    g_monitor_thread.join();
  }

  {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    g_last_clipboard_text.clear();
    g_on_clipboard_changed = nullptr;
  }

  LOG_INFO("Clipboard monitoring stopped");
}

bool Clipboard::IsMonitoring() { return g_monitoring.load(); }

}  // namespace crossdesk
