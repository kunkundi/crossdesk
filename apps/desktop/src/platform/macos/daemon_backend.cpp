#include "platform/daemon_backend.h"

#include <limits.h>
#include <mach-o/dyld.h>

#include <cstdlib>

namespace crossdesk::platform {

bool PrepareDaemon() { return true; }

std::string GetDaemonExecutablePath() {
  char path[PATH_MAX] = {};
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) != 0) return {};
  char resolved[PATH_MAX] = {};
  return realpath(path, resolved) ? std::string(resolved) : std::string(path);
}

}  // namespace crossdesk::platform
