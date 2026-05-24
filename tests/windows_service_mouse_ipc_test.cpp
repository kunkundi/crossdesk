#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(current / "src/service/windows/service_host.cpp")) {
      return current;
    }
    current = current.parent_path();
  }
  return {};
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

bool ExpectContains(const char* name, const std::string& value,
                    const std::string& expected) {
  if (value.find(expected) != std::string::npos) {
    return true;
  }

  std::cerr << name << " missing expected text: " << expected << "\n";
  return false;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string service_host =
      ReadFile(repo_root / "src/service/windows/service_host.cpp");

  bool ok = true;
  ok &= ExpectContains("service_host.cpp", service_host,
                       "ParseSecureDesktopMouseIpcCommand");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "BuildSecureInputHelperMouseCommand");
  ok &= ExpectContains("service_host.cpp", service_host,
                       "return SendSecureDesktopMouseInput");
  return ok ? 0 : 1;
}
