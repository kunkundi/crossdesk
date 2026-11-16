/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CLIPBOARD_FILE_URI_H_
#define _CLIPBOARD_FILE_URI_H_

#include <string>
#include <string_view>
#include <vector>

namespace crossdesk::clipboard_uri {

inline std::string Encode(const std::string& path) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result = "file://";
  for (unsigned char c : path) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
      result += static_cast<char>(c);
    else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
  }
  return result;
}

inline std::vector<std::string> Decode(std::string_view text) {
  const auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::vector<std::string> paths;
  while (!text.empty()) {
    const auto end = text.find('\n');
    auto line = text.substr(0, end);
    text = end == text.npos ? std::string_view{} : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line.front() == '#' || line == "copy" || line == "cut") continue;
    if (line.substr(0, 7) != "file://") return {};
    line.remove_prefix(7);
    if (line.substr(0, 10) == "localhost/") line.remove_prefix(9);
    if (line.empty() || line.front() != '/') return {};
    std::string path;
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (c == '%') {
        if (i + 2 >= line.size() || hex(line[i + 1]) < 0 || hex(line[i + 2]) < 0) return {};
        c = static_cast<char>((hex(line[i + 1]) << 4) | hex(line[i + 2])); i += 2;
      }
      if (c == '\0') return {};
      path += c;
    }
    paths.push_back(std::move(path));
    if (paths.size() > 128) return {};
  }
  return paths;
}

}  // namespace crossdesk::clipboard_uri

#endif
