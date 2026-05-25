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
        std::filesystem::exists(current / "src/gui/toolbars/control_bar.cpp")) {
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

bool ExpectResetBeforeDisplayPopup(const std::string& value) {
  const std::string reset = "props->display_selectable_hovered_ = false;";
  const std::string popup = "ImGui::BeginPopup(\"display\")";
  const size_t reset_pos = value.find(reset);
  const size_t popup_pos = value.find(popup);

  if (reset_pos != std::string::npos && popup_pos != std::string::npos &&
      reset_pos < popup_pos) {
    return true;
  }

  std::cerr << "control_bar.cpp must clear display_selectable_hovered_ before "
               "checking the display popup\n";
  return false;
}

bool ExpectResetBeforeShortcutPopup(const std::string& value) {
  const std::string reset = "props->shortcut_selectable_hovered_ = false;";
  const std::string popup = "ImGui::BeginPopup(\"shortcut\")";
  const size_t reset_pos = value.find(reset);
  const size_t popup_pos = value.find(popup);

  if (reset_pos != std::string::npos && popup_pos != std::string::npos &&
      reset_pos < popup_pos) {
    return true;
  }

  std::cerr << "control_bar.cpp must clear shortcut_selectable_hovered_ before "
               "checking the shortcut popup\n";
  return false;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string control_bar =
      ReadFile(repo_root / "src/gui/toolbars/control_bar.cpp");

  bool ok = true;
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "props->display_selectable_hovered_ = false;");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "ImGui::IsWindowHovered("
                       "ImGuiHoveredFlags_RootAndChildWindows)");
  ok &= ExpectResetBeforeDisplayPopup(control_bar);
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "props->shortcut_selectable_hovered_ =");
  ok &= ExpectResetBeforeShortcutPopup(control_bar);
  return ok ? 0 : 1;
}
