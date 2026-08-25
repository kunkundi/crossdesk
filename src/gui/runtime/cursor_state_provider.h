#ifndef CROSSDESK_GUI_CURSOR_STATE_PROVIDER_H_
#define CROSSDESK_GUI_CURSOR_STATE_PROVIDER_H_

#include <memory>

#include "device_controller.h"

namespace crossdesk {

// Samples the cursor that is currently displayed by the controlled desktop
// and converts platform-specific cursor handles to protocol cursor shapes.
class CursorStateProvider {
 public:
  CursorStateProvider();
  ~CursorStateProvider();

  CursorStateProvider(const CursorStateProvider&) = delete;
  CursorStateProvider& operator=(const CursorStateProvider&) = delete;

  bool Sample(CursorState* state);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crossdesk

#endif  // CROSSDESK_GUI_CURSOR_STATE_PROVIDER_H_
