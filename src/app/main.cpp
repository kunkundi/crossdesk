#ifdef _WIN32
#ifdef DESK_PORT_DEBUG
#pragma comment(linker, "/subsystem:\"console\"")
#else
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif
#endif

#include "rd_log.h"
#include "render.h"

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[]) {
  Render render;
  render.Run();

  return 0;
}