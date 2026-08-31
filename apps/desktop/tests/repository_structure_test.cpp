#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(
            current /
            "libs/wire/include/remote_action.h") &&
        std::filesystem::exists(
            current / "apps/desktop/xmake/targets.lua")) {
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
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

bool IsCheckedWireFile(const std::filesystem::path& path) {
  static constexpr std::array<std::string_view, 8> kExtensions = {
      ".h", ".hpp", ".c", ".cc", ".cpp", ".m", ".mm", ".lua"};
  const std::string extension = path.extension().string();
  for (std::string_view candidate : kExtensions) {
    if (extension == candidate) {
      return true;
    }
  }
  return false;
}

bool CheckWireBoundary(const std::filesystem::path& repo_root) {
  static constexpr std::array<std::string_view, 12> kForbiddenReferences = {
      "apps/desktop", "../desktop", "platform/common", "<windows.h>",
      "<Windows.h>",      "<AppKit/",     "<Cocoa/",       "<CoreGraphics/",
      "<X11/",            "<wayland-",    "<linux/",       "<d3d"};

  bool ok = true;
  const std::filesystem::path wire_root = repo_root / "libs/wire";
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(wire_root)) {
    if (!entry.is_regular_file() || !IsCheckedWireFile(entry.path())) {
      continue;
    }
    const std::string contents = ReadFile(entry.path());
    for (std::string_view forbidden : kForbiddenReferences) {
      if (contents.find(forbidden) == std::string::npos) {
        continue;
      }
      std::cerr << entry.path().lexically_relative(repo_root).string()
                << " crosses the wire/app boundary with: " << forbidden
                << '\n';
      ok = false;
    }
  }
  return ok;
}

bool CheckPlatformIncludesArePrivate(
    const std::filesystem::path& repo_root) {
  const std::filesystem::path targets_path =
      repo_root / "apps/desktop/xmake/targets.lua";
  const std::string targets = ReadFile(targets_path);
  if (targets.empty()) {
    std::cerr << "failed to read "
              << targets_path.lexically_relative(repo_root).string() << '\n';
    return false;
  }

  bool ok = true;
  std::size_t cursor = 0;
  constexpr std::string_view kCall = "add_includedirs(";
  constexpr std::string_view kPlatformPath =
      "apps/desktop/src/platform/";
  while ((cursor = targets.find(kCall, cursor)) != std::string::npos) {
    const std::size_t call_end = targets.find(')', cursor + kCall.size());
    if (call_end == std::string::npos) {
      std::cerr << "unterminated add_includedirs call in "
                << targets_path.lexically_relative(repo_root).string() << '\n';
      return false;
    }

    const std::string_view call(targets.data() + cursor,
                                call_end - cursor + 1);
    std::string compact_call;
    compact_call.reserve(call.size());
    for (char character : call) {
      if (!std::isspace(static_cast<unsigned char>(character))) {
        compact_call.push_back(character);
      }
    }
    if (call.find(kPlatformPath) != std::string_view::npos &&
        compact_call.find("public=true") != std::string::npos) {
      std::cerr << "platform implementation include directory is public: "
                << call << '\n';
      ok = false;
    }
    cursor = call_end + 1;
  }
  return ok;
}

bool CheckWireLayout(const std::filesystem::path& repo_root) {
  static constexpr std::array<std::string_view, 5> kForbiddenPaths = {
      "libs/wire/include/crossdesk", "libs/wire/xmake.lua", "libs/wire/.xmake",
      "xmake.protocol.lua", "xmake.wire.lua"};
  bool ok = true;
  for (std::string_view relative_path : kForbiddenPaths) {
    if (!std::filesystem::exists(repo_root / relative_path)) {
      continue;
    }
    std::cerr << "forbidden legacy layer, standalone project, or wire cache: "
              << relative_path << '\n';
    ok = false;
  }
  return ok;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  bool ok = true;
  static constexpr std::array<std::string_view, 11> kLegacyRootDirectories = {
      "src",       "ios",       "scripts",    "icons",
      "tests",     "xmake",     "shared",     "platforms",
      "wire",      "submodules", "thirdparty"};
  for (std::string_view directory : kLegacyRootDirectories) {
    if (!std::filesystem::exists(repo_root / directory)) {
      continue;
    }
    std::cerr << "legacy root directory still exists: " << directory << '\n';
    ok = false;
  }

  ok &= CheckWireBoundary(repo_root);
  ok &= CheckPlatformIncludesArePrivate(repo_root);
  ok &= CheckWireLayout(repo_root);
  return ok ? 0 : 1;
}
