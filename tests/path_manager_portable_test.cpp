#include "path_manager.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path GetExecutableDirectory() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH] = {};
  DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return {};
  }
  return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
  char buffer[PATH_MAX] = {};
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) != 0) {
    return {};
  }
  return std::filesystem::weakly_canonical(buffer).parent_path();
#else
  char buffer[PATH_MAX] = {};
  ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) {
    return {};
  }
  buffer[length] = '\0';
  return std::filesystem::path(buffer).parent_path();
#endif
}

bool ExpectEqual(const char* name,
                 const std::filesystem::path& actual,
                 const std::filesystem::path& expected) {
  if (actual.lexically_normal() == expected.lexically_normal()) {
    return true;
  }

  std::cerr << name << " mismatch\n"
            << "  expected: " << expected.string() << "\n"
            << "  actual:   " << actual.string() << "\n";
  return false;
}

}  // namespace

int main() {
  const std::filesystem::path exe_dir = GetExecutableDirectory();
  if (exe_dir.empty()) {
    std::cerr << "failed to resolve executable directory\n";
    return 1;
  }

  crossdesk::PathManager path_manager("CrossDesk");
  const std::filesystem::path expected_data = exe_dir / "data";
  const std::filesystem::path expected_logs = exe_dir / "logs";

  bool ok = true;
  ok &= ExpectEqual("config path", path_manager.GetConfigPath(), expected_data);
  ok &= ExpectEqual("cache path", path_manager.GetCachePath(), expected_data);
  ok &= ExpectEqual("log path", path_manager.GetLogPath(), expected_logs);

  return ok ? 0 : 1;
}
