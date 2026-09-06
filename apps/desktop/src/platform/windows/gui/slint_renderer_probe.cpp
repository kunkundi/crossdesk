#include <SDL3/SDL.h>
#include <windows.h>

#include <chrono>
#include <iostream>

#include "crossdesk_ui.h"
#include "platform/windows/gui/slint_backend.h"

namespace crossdesk {

int RunSlintRendererProbe() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return 1;
  }

  int result = 2;
  {
    // Reuse the application's single AOT entry point. Independently generated
    // headers can contain colliding internal component names across TUs.
    auto probe = ui::MainWindow::create();
    probe->set_custom_titlebar(true);
    // AfterRendering confirms that the real GL context, shader initialization
    // and first draw all succeeded. GPU readback from an off-screen window can
    // be black even when rendering works, so do not use its pixels as a test.
    const auto notifier_error = probe->window().set_rendering_notifier(
        [&](slint::RenderingState state, slint::GraphicsAPI) {
          if (state == slint::RenderingState::AfterRendering) {
            result = 0;
          }
        });
    probe->window().set_position(
        slint::PhysicalPosition(slint::Point<int32_t>{-32000, -32000}));
    probe->show();
    slint::Timer timer;
    timer.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(100),
                [&] {
                  // Renderers without a rendering notifier (software) support
                  // snapshots.
                  if (notifier_error) {
                    const auto pixels = probe->window().take_snapshot();
                    if (pixels && pixels->width() > 0 && pixels->height() > 0) {
                      result = 0;
                    } else {
                      std::cerr << "Slint probe could not read rendered pixels"
                                << std::endl;
                    }
                  }
                  slint::quit_event_loop();
                });
    slint::run_event_loop();
    probe->hide();
  }
  SDL_Quit();
  return result;
}

}  // namespace crossdesk
