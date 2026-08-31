#include "platform.h"

#include <ifaddrs.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "rd_log.h"

namespace crossdesk {

std::string GetMac() {
  struct ifaddrs* addresses = nullptr;
  if (getifaddrs(&addresses) != 0) return {};

  std::string result;
  for (struct ifaddrs* cursor = addresses; cursor; cursor = cursor->ifa_next) {
    if (!cursor->ifa_addr || cursor->ifa_addr->sa_family != AF_LINK ||
        std::strcmp(cursor->ifa_name, "en0") != 0) {
      continue;
    }
    const auto* link =
        reinterpret_cast<const struct sockaddr_dl*>(cursor->ifa_addr);
    if (link->sdl_type != IFT_ETHER) continue;
    const auto* bytes = reinterpret_cast<const unsigned char*>(
        &link->sdl_data[link->sdl_nlen]);
    char address[16] = {};
    int length = 0;
    for (int index = 0; index < link->sdl_alen; ++index) {
      length += std::snprintf(address + length, sizeof(address) - length,
                              "%.2X", bytes[index]);
    }
    result = address;
    break;
  }
  freeifaddrs(addresses);
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

bool IsWaylandSession() { return false; }

}  // namespace crossdesk
