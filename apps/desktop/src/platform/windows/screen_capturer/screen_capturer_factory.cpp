#include "screen_capturer_factory.h"

#include "screen_capturer_win.h"

namespace crossdesk {

ScreenCapturer* ScreenCapturerFactory::Create() {
  return new ScreenCapturerWin();
}

}  // namespace crossdesk
