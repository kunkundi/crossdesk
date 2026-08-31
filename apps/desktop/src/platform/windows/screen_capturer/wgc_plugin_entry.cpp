#include <mutex>

#include "path_manager.h"
#include "rd_log.h"
#include "screen_capturer_wgc.h"
#include "wgc_plugin_api.h"

namespace {

void InitializePluginLogger() {
  static std::once_flag once;
  std::call_once(once, []() {
    crossdesk::PathManager path_manager("CrossDesk");
    crossdesk::InitLogger(path_manager.GetLogPath().string());
  });
}

}  // namespace

extern "C" {

crossdesk::ScreenCapturer* CrossDeskCreateWgcCapturer() {
  InitializePluginLogger();
  return new crossdesk::ScreenCapturerWgc();
}

void CrossDeskDestroyWgcCapturer(crossdesk::ScreenCapturer* capturer) {
  delete capturer;
}
}
