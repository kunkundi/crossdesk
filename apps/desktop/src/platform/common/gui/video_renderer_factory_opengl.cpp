#include <memory>
#ifdef _WIN32
#include <d3d11.h>
#include "platform/windows/screen_capturer/hdr_display.h"
#endif

#include "platform/common/gui/opengl_video_renderer.h"
#include "platform/video_renderer.h"

namespace crossdesk {

#ifdef _WIN32
namespace {
BOOL CALLBACK CheckHdrDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
  if (!IsHdrMonitor(monitor)) return TRUE;
  *reinterpret_cast<bool*>(data) = true;
  return FALSE;
}
}  // namespace
bool HasHdrVideoDisplay() {
  bool available = false;
  EnumDisplayMonitors(nullptr, nullptr, &CheckHdrDisplay, reinterpret_cast<LPARAM>(&available));
  return available;
}
#endif

std::unique_ptr<VideoRenderer> CreateVideoRenderer() {
  return std::make_unique<OpenGlVideoRenderer>();
}

}  // namespace crossdesk
