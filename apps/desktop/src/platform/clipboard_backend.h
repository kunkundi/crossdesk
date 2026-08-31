#ifndef CROSSDESK_PLATFORM_CLIPBOARD_BACKEND_H_
#define CROSSDESK_PLATFORM_CLIPBOARD_BACKEND_H_

namespace crossdesk::platform {

bool ClipboardMonitoring();
void HandleClipboardChange();
void RunClipboardMonitor(int check_interval_ms);
void WakeClipboardMonitor();

}  // namespace crossdesk::platform

#endif  // CROSSDESK_PLATFORM_CLIPBOARD_BACKEND_H_
