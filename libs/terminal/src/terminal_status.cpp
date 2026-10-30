#include "terminal_renderer.h"
#include <algorithm>

namespace crossdesk::terminal {
namespace {
std::string Clip(const std::string &text, size_t width) {
  if (text.size() <= width)
    return text;
  return width > 3 ? text.substr(0, width - 3) + "..." : text.substr(0, width);
}

std::string Compact(const std::vector<std::string> &choices, size_t width) {
  for (const auto &text : choices)
    if (text.size() <= width)
      return text;
  return {};
}

std::string Label(const std::string &label) {
  std::string text = label;
  text.resize(5, ' ');
  return text + " | ";
}

std::string CompactRow(const std::string &label,
                       const std::vector<std::string> &choices, size_t width) {
  const auto prefix = width >= 12 ? Label(label) : std::string{};
  return prefix + Compact(choices, width - prefix.size());
}

std::string Columns(const std::string &label,
                    const std::vector<std::string> &cells, size_t width,
                    size_t count) {
  std::string line = Label(label);
  const size_t content = width - line.size() - (count - 1) * 3;
  for (size_t i = 0; i < cells.size(); ++i) {
    const size_t start = 8 + i * content / count + i * 3;
    if (i) {
      line.resize(start - 3, ' ');
      line += " | ";
    }
    line += cells[i];
  }
  return line;
}
} // namespace

std::vector<std::string> StatusLines(const StatusBar &bar, int columns) {
  const size_t width = std::clamp(columns, 1, 240);
  const size_t margin = width >= 20 ? 1 : 0;
  const size_t inner = width - 2 * margin;
  const std::string mode =
      bar.command_mode
          ? (bar.control
                 ? (inner < 36 ? "LOCAL PAUSED" : "LOCAL / CONTROL PAUSED")
                 : (inner < 36 ? "LOCAL READ" : "LOCAL / READ ONLY"))
      : bar.control ? "CONTROL"
                    : "READ ONLY";
  const auto progress =
      bar.error.empty() ? bar.progress : "Error: " + bar.error;
  auto left = mode + " | " + progress;
  auto right = bar.view + (bar.network.empty() ? "" : " | " + bar.network);
  if (left.size() + right.size() + 2 > inner)
    right = bar.network;
  if (left.size() + right.size() + 2 > inner && bar.error.empty())
    left = mode + " | " + bar.compact_progress;
  if (left.size() + right.size() + 2 > inner)
    right.clear();
  std::string status = Clip(left, inner);
  if (!right.empty()) {
    status.resize(inner - right.size(), ' ');
    status += right;
  }

  std::string keys, view;
  if (inner >= 63) {
    const size_t count = inner >= 99 ? 4 : inner >= 77 ? 3 : 2;
    if (bar.command_mode) {
      keys =
          Columns("LOCAL",
                  count >= 3 ? std::vector<std::string>{"<Esc/Ctrl+C> Return",
                                                        "<c> Toggle control",
                                                        "<q> Quit"}
                             : std::vector<std::string>{"<Esc/Ctrl+C> Return",
                                                        "<c> Toggle; <q> Quit"},
                  inner, count);
    } else if (bar.control) {
      keys = Columns("KEYS", {"<Ctrl+]> Commands", "Input -> remote"}, inner,
                     count);
    } else {
      keys = Columns("KEYS",
                     count >= 3
                         ? std::vector<std::string>{"<c> Control", "<q> Quit",
                                                    "<Ctrl+]> Commands"}
                         : std::vector<std::string>{"<c> Control; <q> Quit",
                                                    "<Ctrl+]> Commands"},
                     inner, count);
    }
    if (bar.control && !bar.command_mode) {
      view = Columns("THEN", {"<c> Toggle control", "<q> Quit"}, inner, count);
    } else {
      view = Columns(
          "VIEW",
          count == 4
              ? std::vector<std::string>{"<arrows/ijkl> Pan", "<+/-> Zoom",
                                         "<f> Fit", "<d> Detail"}
          : count == 3
              ? std::vector<std::string>{"<arrows/ijkl> Pan", "<+/-> Zoom",
                                         "<f> Fit; <d> Detail"}
              : std::vector<std::string>{"<arrows> Pan; <+/-> Zoom",
                                         "<f> Fit; <d> Detail"},
          inner, count);
    }
  } else {
    keys = CompactRow(
        bar.command_mode ? "LOCAL" : "KEYS",
        bar.command_mode
            ? std::vector<std::string>{"<Esc> Return | <c> Toggle | <q> Quit",
                                       "<Esc> Return | <q> Quit",
                                       "<Esc> Back <q> Quit", "<q> Quit", "<q>",
                                       "q"}
        : bar.control
            ? std::vector<std::string>{"<Ctrl+]> Commands | Input -> remote",
                                       "<Ctrl+]> Commands", "<Ctrl+]> Menu",
                                       "<Ctrl+]>"}
            : std::vector<
                  std::string>{"<c> Control | <q> Quit | <Ctrl+]> Commands",
                               "<c> Control | <q> Quit", "<c> Ctrl <q> Quit",
                               "<q> Quit", "<q>", "q"},
        inner);
    view = CompactRow(
        bar.control && !bar.command_mode ? "THEN" : "VIEW",
        bar.control && !bar.command_mode
            ? std::vector<std::string>{"<c> Toggle control | <q> Quit",
                                       "<c> Toggle; <q> Quit", "<c> / <q>",
                                       "<q>", "q"}
            : std::vector<std::string>{"<arrows> Pan | <+/-> Zoom | <f> Fit | "
                                       "<d> Detail",
                                       "<+/-> Zoom | <f> Fit | <d> Detail",
                                       "<f> Fit | <d> Detail", "<f> Fit", "<f>",
                                       "f"},
        inner);
  }
  std::vector<std::string> lines{status, keys, view};
  for (auto &line : lines) {
    line.insert(0, margin, ' ');
    line.resize(width, ' ');
  }
  return lines;
}
} // namespace crossdesk::terminal
