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
        std::filesystem::exists(current / "src/gui/ui/stream_window.slint")) {
      return current;
    }
    current = current.parent_path();
  }
  return {};
}

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

bool ExpectContains(const char *name, const std::string &value,
                    const std::string &expected) {
  if (value.find(expected) != std::string::npos) {
    return true;
  }
  std::cerr << name << " missing expected text: " << expected << "\n";
  return false;
}

bool ExpectNotContains(const char *name, const std::string &value,
                       const std::string &unexpected) {
  if (value.find(unexpected) == std::string::npos) {
    return true;
  }
  std::cerr << name << " contains unexpected text: " << unexpected << "\n";
  return false;
}

bool ExpectContainsAtLeast(const char *name, const std::string &value,
                           const std::string &expected, size_t min_count) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = value.find(expected, pos)) != std::string::npos) {
    ++count;
    pos += expected.size();
  }
  if (count >= min_count) {
    return true;
  }
  std::cerr << name << " expected at least " << min_count
            << " occurrences of: " << expected << ", found " << count << "\n";
  return false;
}

} // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string stream =
      ReadFile(repo_root / "src/gui/ui/stream_window.slint");
  const std::string common = ReadFile(repo_root / "src/gui/ui/common.slint");
  const std::string application =
      ReadFile(repo_root / "src/gui/application/gui_application.cpp");

  bool ok = true;
  ok &= ExpectContains("stream_window.slint", stream,
                       "private property <bool> display-menu-open: false;");
  ok &= ExpectContains("stream_window.slint", stream,
                       "root.shortcut-menu-open = false;");
  ok &= ExpectContains("stream_window.slint", stream,
                       "if root.display-menu-open: Rectangle");
  ok &= ExpectContains("stream_window.slint", stream,
                       "display-touch.has-hover");
  ok &= ExpectContains("stream_window.slint", stream,
                       "root.switch-display(index)");
  ok &= ExpectContains("stream_window.slint", stream,
                       "if root.shortcut-menu-open: Rectangle");
  ok &= ExpectContains("stream_window.slint", stream,
                       "shortcut-touch.has-hover");

  ok &= ExpectContains("common.slint", common,
                       "if touch.has-hover && root.tooltip != \"\": Rectangle");
  ok &= ExpectContainsAtLeast("stream_window.slint", stream, "tooltip:", 9);
  ok &= ExpectContains("stream_window.slint", stream,
                       "tooltip: StreamStrings.select-display");
  ok &= ExpectContains("stream_window.slint", stream,
                       "tooltip: StreamStrings.send-shortcut");
  ok &= ExpectContains("stream_window.slint", stream,
                       "root.mouse-control-enabled ? StreamStrings.release-mouse : StreamStrings.control-mouse");
  ok &= ExpectContains("stream_window.slint", stream,
                       "root.audio-enabled ? StreamStrings.mute : StreamStrings.audio");
  ok &= ExpectContains("stream_window.slint", stream,
                       "tooltip: StreamStrings.select-file");
  ok &= ExpectContains("stream_window.slint", stream,
                       "root.stats-visible ? StreamStrings.hide-stats : StreamStrings.show-stats");
  ok &= ExpectContains("stream_window.slint", stream,
                       "root.fullscreen-enabled ? StreamStrings.exit-fullscreen : StreamStrings.fullscreen");
  ok &= ExpectContains("stream_window.slint", stream,
                       "tooltip: StreamStrings.disconnect");
  ok &= ExpectContains("stream_window.slint", stream,
                       "root.control-expanded ? StreamStrings.collapse-control : StreamStrings.expand-control");

  ok &= ExpectContains("gui_application.cpp", application,
                       "set_select_display(");
  ok &= ExpectContains("gui_application.cpp", application,
                       "set_show_stats(");
  ok &= ExpectContains("gui_application.cpp", application,
                       "set_expand_control(");
  ok &= ExpectContains("gui_application.cpp", application,
                       "set_collapse_control(");
  ok &= ExpectContains("gui_application.cpp", application,
                       "(*ui_->stream)->global<ui::StreamStrings>()");
  ok &= ExpectNotContains("gui_application.cpp", application,
                          "#include <imgui");
  return ok ? 0 : 1;
}
