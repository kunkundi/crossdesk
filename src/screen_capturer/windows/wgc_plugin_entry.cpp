#include "screen_capturer_wgc.h"
#include "wgc_plugin_api.h"

extern "C" {

crossdesk::ScreenCapturer* CrossDeskCreateWgcCapturer() {
  return new crossdesk::ScreenCapturerWgc();
}

void CrossDeskDestroyWgcCapturer(crossdesk::ScreenCapturer* capturer) {
  delete capturer;
}
}
