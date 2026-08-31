#include "device_controller_factory.h"

#include "keyboard_capturer.h"
#include "mouse_controller.h"

namespace crossdesk {

DeviceController* DeviceControllerFactory::Create(Device device) {
  switch (device) {
    case Mouse:
      return new PlatformMouseController();
    case Keyboard:
      return new PlatformKeyboardCapturer();
    default:
      return nullptr;
  }
}

}  // namespace crossdesk
