#include "screen_capturer_factory.h"

#include "screen_capturer_sck.h"

namespace crossdesk {

ScreenCapturer* ScreenCapturerFactory::Create() {
  return new ScreenCapturerSck();
}

}  // namespace crossdesk
