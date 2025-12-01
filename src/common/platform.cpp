#include "platform.h"

#include "rd_log.h"

#ifdef _WIN32
#include <Winsock2.h>
#include <iphlpapi.h>
#elif __APPLE__
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <sys/socket.h>
#include <sys/types.h>
#elif __linux__
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace crossdesk {

std::string GetMac() {
  char mac_addr[16];
  int len = 0;
#ifdef _WIN32
  IP_ADAPTER_INFO adapterInfo[16];
  DWORD bufferSize = sizeof(adapterInfo);
  DWORD result = GetAdaptersInfo(adapterInfo, &bufferSize);
  if (result == ERROR_SUCCESS) {
    PIP_ADAPTER_INFO adapter = adapterInfo;
    while (adapter) {
      for (UINT i = 0; i < adapter->AddressLength; i++) {
        len += sprintf_s(mac_addr + len, sizeof(mac_addr) - len, "%.2X",
                         adapter->Address[i]);
      }
      break;
    }
  }
#elif __APPLE__
  std::string if_name = "en0";

  struct ifaddrs* addrs;
  struct ifaddrs* cursor;
  const struct sockaddr_dl* dlAddr;

  if (!getifaddrs(&addrs)) {
    cursor = addrs;
    while (cursor != 0) {
      const struct sockaddr_dl* socAddr =
          (const struct sockaddr_dl*)cursor->ifa_addr;
      if ((cursor->ifa_addr->sa_family == AF_LINK) &&
          (socAddr->sdl_type == IFT_ETHER) &&
          strcmp(if_name.c_str(), cursor->ifa_name) == 0) {
        dlAddr = (const struct sockaddr_dl*)cursor->ifa_addr;
        const unsigned char* base =
            (const unsigned char*)&dlAddr->sdl_data[dlAddr->sdl_nlen];
        for (int i = 0; i < dlAddr->sdl_alen; i++) {
          len +=
              snprintf(mac_addr + len, sizeof(mac_addr) - len, "%.2X", base[i]);
        }
      }
      cursor = cursor->ifa_next;
    }
    freeifaddrs(addrs);
  }
#elif __linux__
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    return "";
  }
  struct ifreq ifr;
  struct ifconf ifc;
  char buf[1024];
  ifc.ifc_len = sizeof(buf);
  ifc.ifc_buf = buf;
  if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
    close(sock);
    return "";
  }
  struct ifreq* it = ifc.ifc_req;
  const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
  for (; it != end; ++it) {
    std::strcpy(ifr.ifr_name, it->ifr_name);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
      continue;
    }
    if (ifr.ifr_flags & IFF_LOOPBACK) {
      continue;
    }
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
      continue;
    }
    std::string mac_address;
    for (int i = 0; i < 6; ++i) {
      len += sprintf(mac_addr + len, "%.2X", ifr.ifr_hwaddr.sa_data[i] & 0xff);
    }
    break;
  }
  close(sock);
#endif
  return mac_addr;
}

std::string GetHostName() {
  char hostname[256];
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed." << std::endl;
    return "";
  }
  if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
    LOG_ERROR("gethostname failed: {}", WSAGetLastError());
    WSACleanup();
    return "";
  }
  WSACleanup();
#else
  if (gethostname(hostname, sizeof(hostname)) == -1) {
    LOG_ERROR("gethostname failed");
    return "";
  }
#endif
  return hostname;
}
}  // namespace crossdesk