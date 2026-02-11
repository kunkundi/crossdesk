#ifdef _WIN32
#ifdef CROSSDESK_DEBUG
#pragma comment(linker, "/subsystem:\"console\"")
#else
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif
#endif

#include <cstring>
#include <memory>
#include <string>

#include "config_center.h"
#include "daemon.h"
#include "path_manager.h"
#include "render.h"

int main(int argc, char* argv[]) {
  // check if running as child process
  bool is_child = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--child") == 0) {
      is_child = true;
      break;
    }
  }

  if (is_child) {
    // child process: run render directly
    crossdesk::Render render;
    render.Run();
    return 0;
  }

  bool enable_daemon = false;
  auto path_manager = std::make_unique<crossdesk::PathManager>("CrossDesk");
  if (path_manager) {
    std::string cache_path = path_manager->GetCachePath().string();
    crossdesk::ConfigCenter config_center(cache_path + "/config.ini");
    enable_daemon = config_center.IsEnableDaemon();
  }

  if (enable_daemon) {
    // start daemon with restart monitoring
    Daemon daemon("CrossDesk");

    // define main loop function: run render and stop daemon on normal exit
    Daemon::MainLoopFunc main_loop = [&daemon]() {
      crossdesk::Render render;
      render.Run();
      daemon.stop();
    };

    // start daemon and return result
    bool success = daemon.start(main_loop);
    return success ? 0 : 1;
  }

  // run without daemon: direct execution
  crossdesk::Render render;
  render.Run();
  return 0;
}