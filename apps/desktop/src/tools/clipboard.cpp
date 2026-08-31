#include "clipboard.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "platform/clipboard_backend.h"
#include "rd_log.h"

namespace crossdesk {
namespace {

std::atomic<bool> g_monitoring{false};
std::thread g_monitor_thread;
std::mutex g_monitor_mutex;
std::string g_last_clipboard_text;
Clipboard::OnClipboardChanged g_on_clipboard_changed;

}  // namespace

namespace platform {

bool ClipboardMonitoring() { return g_monitoring.load(); }

void HandleClipboardChange() {
  if (!Clipboard::HasText()) {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    if (!g_last_clipboard_text.empty()) {
      g_last_clipboard_text.clear();
      LOG_INFO("Clipboard content cleared");
    }
    return;
  }

  const std::string current_text = Clipboard::GetText();
  std::lock_guard<std::mutex> lock(g_monitor_mutex);
  if (current_text == g_last_clipboard_text) return;
  g_last_clipboard_text = current_text;
  if (!current_text.empty() && g_on_clipboard_changed) {
    const int result =
        g_on_clipboard_changed(current_text.c_str(), current_text.length());
    if (result != 0) {
      LOG_WARN("Clipboard callback returned error: {}", result);
    }
  }
}

}  // namespace platform

void Clipboard::StartMonitoring(int check_interval_ms,
                                OnClipboardChanged on_changed) {
  if (g_monitoring.load()) {
    LOG_WARN("Clipboard monitoring is already running");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    g_on_clipboard_changed = std::move(on_changed);
    g_last_clipboard_text = HasText() ? GetText() : std::string();
  }
  g_monitoring.store(true);
  g_monitor_thread = std::thread(
      [check_interval_ms] { platform::RunClipboardMonitor(check_interval_ms); });
  LOG_INFO("Clipboard event monitoring started");
}

void Clipboard::StopMonitoring() {
  if (!g_monitoring.exchange(false)) return;

  platform::WakeClipboardMonitor();
  if (g_monitor_thread.joinable()) g_monitor_thread.join();

  {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    g_last_clipboard_text.clear();
    g_on_clipboard_changed = nullptr;
  }
  LOG_INFO("Clipboard monitoring stopped");
}

bool Clipboard::IsMonitoring() { return g_monitoring.load(); }

}  // namespace crossdesk
