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
        std::filesystem::exists(current /
                                "src/gui/views/toolbars/control_bar.cpp")) {
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

bool ExpectContainsAtLeast(const char* name, const std::string& value,
                           const std::string& expected, size_t min_count) {
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
      ReadFile(repo_root / "src/gui/views/toolbars/control_bar.cpp");

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
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "void ShowControlBarTooltip(const std::string& text)");
  ok &= ExpectContainsAtLeast("control_bar.cpp", control_bar,
                              "ShowControlBarTooltip(", 10);
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::select_display"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::send_shortcut"
                       "[localization_language_index_]");
  ok &= ExpectNotContains("control_bar.cpp", control_bar,
                          "ShowControlBarTooltip("
                          "props->mouse_control_button_label_)");
  ok &= ExpectNotContains("control_bar.cpp", control_bar,
                          "ShowControlBarTooltip("
                          "props->audio_capture_button_label_)");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::select_file"
                       "[localization_language_index_]");
  ok &= ExpectNotContains("control_bar.cpp", control_bar,
                          "ShowControlBarTooltip("
                          "props->net_traffic_stats_button_label_)");
  ok &= ExpectNotContains("control_bar.cpp", control_bar,
                          "ShowControlBarTooltip("
                          "props->fullscreen_button_label_)");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::release_mouse"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::control_mouse"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::audio_capture"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::mute[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::hide_net_traffic_stats"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::show_net_traffic_stats"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::exit_fullscreen"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::fullscreen"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::disconnect"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::expand_control_bar"
                       "[localization_language_index_]");
  ok &= ExpectContains("control_bar.cpp", control_bar,
                       "localization::collapse_control_bar"
                       "[localization_language_index_]");
  return ok ? 0 : 1;
}
