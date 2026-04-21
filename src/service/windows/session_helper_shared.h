#ifndef _CROSSDESK_SESSION_HELPER_SHARED_H_
#define _CROSSDESK_SESSION_HELPER_SHARED_H_

#include <Windows.h>

#include <cstdint>
#include <string>

namespace crossdesk {

inline constexpr wchar_t kCrossDeskSessionHelperPipePrefix[] =
    L"\\\\.\\pipe\\CrossDeskSessionHelper-";
inline constexpr wchar_t kCrossDeskSecureInputHelperPipePrefix[] =
    L"\\\\.\\pipe\\CrossDeskSecureInputHelper-";
inline constexpr char kCrossDeskSessionHelperStatusCommand[] = "status";
inline constexpr char kCrossDeskSecureInputKeyboardCommandPrefix[] =
    "keyboard:";
inline constexpr char kCrossDeskSecureInputMouseCommandPrefix[] = "mouse:";
inline constexpr char kCrossDeskSecureInputCaptureCommandPrefix[] = "capture:";
inline constexpr DWORD kCrossDeskSecureInputPipeBufferBytes = 16 * 1024 * 1024;
inline constexpr uint32_t kCrossDeskSecureDesktopFrameMagic = 0x50444358;
inline constexpr uint32_t kCrossDeskSecureDesktopFrameVersion = 1;

#pragma pack(push, 1)
struct CrossDeskSecureDesktopFrameHeader {
  uint32_t magic;
  uint32_t version;
  int32_t left;
  int32_t top;
  uint32_t width;
  uint32_t height;
  uint32_t payload_size;
};
#pragma pack(pop)

inline std::wstring GetCrossDeskSessionHelperPipeName(DWORD session_id) {
  return std::wstring(kCrossDeskSessionHelperPipePrefix) +
         std::to_wstring(session_id);
}

inline std::wstring GetCrossDeskSecureInputHelperPipeName(DWORD session_id) {
  return std::wstring(kCrossDeskSecureInputHelperPipePrefix) +
         std::to_wstring(session_id);
}

}  // namespace crossdesk

#endif