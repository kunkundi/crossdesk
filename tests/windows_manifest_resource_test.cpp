#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(current / "scripts/windows/crossdesk.rc")) {
      return current;
    }
    current = current.parent_path();
  }
  return {};
}

bool ExpectContains(const char* name, const std::string& value,
                    const std::string& expected) {
  if (value.find(expected) != std::string::npos) {
    return true;
  }

  std::cerr << name << " missing expected text: " << expected << "\n";
  return false;
}

bool ExpectNotContains(const char* name, const std::string& value,
                       const std::string& unexpected) {
  if (value.find(unexpected) == std::string::npos) {
    return true;
  }

  std::cerr << name << " contains unexpected text: " << unexpected << "\n";
  return false;
}

#ifdef _WIN32
bool ExpectActivationContext(const std::filesystem::path& manifest_path) {
  ACTCTXW context = {};
  context.cbSize = sizeof(context);
  std::wstring source = manifest_path.wstring();
  context.lpSource = source.c_str();

  HANDLE activation_context = CreateActCtxW(&context);
  if (activation_context == INVALID_HANDLE_VALUE) {
    std::cerr << "CreateActCtxW failed for " << manifest_path.string()
              << ", error=" << GetLastError() << "\n";
    return false;
  }

  ReleaseActCtx(activation_context);
  return true;
}
#endif

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string rc = ReadFile(repo_root / "scripts/windows/crossdesk.rc");
  const std::string manifest =
      ReadFile(repo_root / "scripts/windows/crossdesk.manifest");
  const std::string debug_manifest =
      ReadFile(repo_root / "scripts/windows/crossdesk_debug.manifest");

  bool ok = true;
  ok &= ExpectContains("crossdesk.rc", rc, "crossdesk.manifest");
  ok &= ExpectContains("crossdesk.rc", rc, "crossdesk_debug.manifest");
  ok &= ExpectContains("crossdesk.rc", rc, "CROSSDESK_DEBUG");
  ok &= ExpectContains("crossdesk.rc", rc, "RT_MANIFEST");
  ok &= ExpectContains("crossdesk.manifest", manifest,
                       "level=\"requireAdministrator\"");
  ok &= ExpectContains("crossdesk.manifest", manifest,
                       "http://schemas.microsoft.com/SMI/2005/WindowsSettings");
  ok &= ExpectContains("crossdesk.manifest", manifest,
                       "http://schemas.microsoft.com/SMI/2016/WindowsSettings");
  ok &= ExpectNotContains("crossdesk.manifest", manifest,
                          "processorArchitecture=\"*\"");
  ok &= ExpectContains("crossdesk_debug.manifest", debug_manifest,
                       "level=\"asInvoker\"");
  ok &= ExpectContains("crossdesk_debug.manifest", debug_manifest,
                       "http://schemas.microsoft.com/SMI/2005/WindowsSettings");
  ok &= ExpectContains("crossdesk_debug.manifest", debug_manifest,
                       "http://schemas.microsoft.com/SMI/2016/WindowsSettings");
  ok &= ExpectNotContains("crossdesk_debug.manifest", debug_manifest,
                          "processorArchitecture=\"*\"");
#ifdef _WIN32
  ok &= ExpectActivationContext(repo_root / "scripts/windows/crossdesk.manifest");
  ok &= ExpectActivationContext(
      repo_root / "scripts/windows/crossdesk_debug.manifest");
#endif
  return ok ? 0 : 1;
}
