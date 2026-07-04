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
        std::filesystem::exists(current / "submodules/minirtc/src/api/minirtc.h")) {
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

bool ExpectNotContains(const char* name, const std::string& value,
                       const std::string& unexpected) {
  if (value.find(unexpected) == std::string::npos) {
    return true;
  }

  std::cerr << name << " contains unexpected text: " << unexpected << "\n";
  return false;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string minirtc_api =
      ReadFile(repo_root / "submodules/minirtc/src/api/minirtc.h");
  const std::string peer_connection =
      ReadFile(repo_root / "submodules/minirtc/src/pc/peer_connection.cpp");
  const std::string connection_status_window =
      ReadFile(repo_root / "src/gui/windows/connection_status_window.cpp");
  const std::string render_h = ReadFile(repo_root / "src/gui/render.h");
  const std::string render_cpp = ReadFile(repo_root / "src/gui/render.cpp");
  const std::string remote_peer_panel =
      ReadFile(repo_root / "src/gui/panels/remote_peer_panel.cpp");

  bool ok = true;
  ok &= ExpectContains("minirtc.h", minirtc_api, "RemoteUnavailable");
  ok &= ExpectNotContains("minirtc.h", minirtc_api, "DeviceOffline");
  ok &= ExpectContains("peer_connection.cpp", peer_connection,
                       "\"Remote unavailable\"");
  ok &= ExpectContains("peer_connection.cpp", peer_connection,
                       "ConnectionStatus::RemoteUnavailable");
  ok &= ExpectNotContains("peer_connection.cpp", peer_connection,
                          "\"Device offline\"");
  ok &= ExpectNotContains("peer_connection.cpp", peer_connection,
                          "ConnectionStatus::DeviceOffline");
  ok &= ExpectContains("connection_status_window.cpp",
                       connection_status_window,
                       "localization::device_offline");
  ok &= ExpectContains("render.h", render_h,
                       "pending_presence_probe_started_at_");
  ok &= ExpectContains("render.h", render_h,
                       "connection_attempt_started_at_");
  ok &= ExpectContains("render.cpp", render_cpp,
                       "HandleConnectionTimeouts");
  ok &= ExpectContains("render.cpp", render_cpp,
                       "kPresenceProbeTimeout");
  ok &= ExpectContains("render.cpp", render_cpp,
                       "kConnectionAttemptTimeout");
  ok &= ExpectContains("remote_peer_panel.cpp", remote_peer_panel,
                       "connection_attempt_started_at_");

  return ok ? 0 : 1;
}
