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
        std::filesystem::exists(
            current /
            "apps/desktop/src/platform/windows/input/mouse/mouse_controller.cpp")) {
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

  const std::string mouse_controller = ReadFile(
      repo_root / "apps/desktop/src/platform/windows/input/mouse/mouse_controller.cpp");

  bool ok = true;
  ok &= ExpectContains("mouse_controller.cpp", mouse_controller,
                       "INPUT ip = {0};");
  ok &= ExpectContains("mouse_controller.cpp", mouse_controller,
                       "SetCursorPos failed");
  ok &= ExpectContains("mouse_controller.cpp", mouse_controller,
                       "SendInput failed for mouse");
  return ok ? 0 : 1;
}
