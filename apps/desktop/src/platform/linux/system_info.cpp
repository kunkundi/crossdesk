#include "platform.h"

#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rd_log.h"

namespace crossdesk {

std::string GetMac() {
  const int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) return {};

  char buffer[1024] = {};
  struct ifconf config = {};
  config.ifc_len = sizeof(buffer);
  config.ifc_buf = buffer;
  if (ioctl(socket_fd, SIOCGIFCONF, &config) < 0) {
    close(socket_fd);
    return {};
  }

  std::string result;
  struct ifreq request = {};
  struct ifreq* cursor = config.ifc_req;
  const struct ifreq* end = cursor + config.ifc_len / sizeof(struct ifreq);
  for (; cursor != end; ++cursor) {
    std::strncpy(request.ifr_name, cursor->ifr_name,
                 sizeof(request.ifr_name) - 1);
    if (ioctl(socket_fd, SIOCGIFFLAGS, &request) < 0 ||
        (request.ifr_flags & IFF_LOOPBACK) ||
        ioctl(socket_fd, SIOCGIFHWADDR, &request) < 0) {
      continue;
    }
    char address[16] = {};
    int length = 0;
    for (int index = 0; index < 6; ++index) {
      length += std::snprintf(
          address + length, sizeof(address) - length, "%.2X",
          static_cast<unsigned char>(request.ifr_hwaddr.sa_data[index]));
    }
    result = address;
    break;
  }
  close(socket_fd);
  return result;
}

std::string GetHostName() {
  char hostname[256] = {};
  if (gethostname(hostname, sizeof(hostname)) == -1) {
    LOG_ERROR("gethostname failed");
    return {};
  }
  return hostname;
}

bool IsWaylandSession() {
  const char* session_type = std::getenv("XDG_SESSION_TYPE");
  if (session_type) {
    if (std::strcmp(session_type, "wayland") == 0 ||
        std::strcmp(session_type, "Wayland") == 0) {
      return true;
    }
    if (std::strcmp(session_type, "x11") == 0 ||
        std::strcmp(session_type, "X11") == 0) {
      return false;
    }
  }
  const char* display = std::getenv("WAYLAND_DISPLAY");
  return display && display[0] != '\0';
}

}  // namespace crossdesk
