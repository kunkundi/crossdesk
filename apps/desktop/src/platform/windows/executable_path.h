#ifndef CROSSDESK_PLATFORM_WINDOWS_EXECUTABLE_PATH_H_
#define CROSSDESK_PLATFORM_WINDOWS_EXECUTABLE_PATH_H_

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace crossdesk::platform {

// Full path of this process image, including long paths; empty on failure.
inline std::wstring CurrentExecutablePath() {
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                          static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  return {path.data(), length};
}

// A file installed next to this executable; empty if the path is unknown.
inline std::filesystem::path SiblingExecutablePath(const wchar_t* name) {
  const auto executable = CurrentExecutablePath();
  if (executable.empty()) return {};
  return std::filesystem::path(executable).parent_path() / name;
}

}  // namespace crossdesk::platform

#endif
