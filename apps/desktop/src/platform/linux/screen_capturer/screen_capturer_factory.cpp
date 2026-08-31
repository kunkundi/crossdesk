#include "screen_capturer_factory.h"

#include "screen_capturer_linux.h"

namespace crossdesk {

ScreenCapturer* ScreenCapturerFactory::Create() {
  return new ScreenCapturerLinux();
}

}  // namespace crossdesk
