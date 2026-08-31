#include "platform.h"

#include <Winsock2.h>
#include <iphlpapi.h>

#include <cstdio>

#include "rd_log.h"

namespace crossdesk {

std::string GetMac() {
  IP_ADAPTER_INFO adapters[16] = {};
  DWORD size = sizeof(adapters);
  if (GetAdaptersInfo(adapters, &size) != ERROR_SUCCESS) return {};

  char address[16] = {};
  int length = 0;
  const PIP_ADAPTER_INFO adapter = adapters;
  for (UINT index = 0; index < adapter->AddressLength; ++index) {
    length += sprintf_s(address + length, sizeof(address) - length, "%.2X",
                        adapter->Address[index]);
  }
  return address;
}

std::string GetHostName() {
  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    LOG_ERROR("WSAStartup failed");
    return {};
  }
  char hostname[256] = {};
  if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
    LOG_ERROR("gethostname failed: {}", WSAGetLastError());
    WSACleanup();
    return {};
  }
  WSACleanup();
  return hostname;
}

bool IsWaylandSession() { return false; }

}  // namespace crossdesk
