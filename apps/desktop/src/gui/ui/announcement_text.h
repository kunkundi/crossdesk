/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-29
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ANNOUNCEMENT_TEXT_H_
#define _ANNOUNCEMENT_TEXT_H_

#include <slint.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace crossdesk::announcement_text {

inline size_t WebSchemeLength(std::string_view url) {
  for (const std::string_view prefix : {"https://", "http://"}) {
    if (url.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), url.begin(),
                   [](char expected, unsigned char actual) {
                     return expected == std::tolower(actual);
                   })) {
      return prefix.size();
    }
  }
  return 0;
}

inline bool IsWebUrl(std::string_view url) {
  const size_t scheme = WebSchemeLength(url);
  if (!scheme || url.size() == scheme ||
      std::any_of(url.begin(), url.end(), [](unsigned char ch) {
        return ch <= 0x20 || ch == 0x7f || ch == '\\' || ch == '<' ||
               ch == '>' || ch == '"';
      })) {
    return false;
  }
  const auto authority =
      url.substr(scheme, url.find_first_of("/?#", scheme) - scheme);
  return !authority.empty() && authority.back() != '@';
}

namespace detail {

inline size_t UrlEnd(std::string_view text, size_t start) {
  // Do not include surrounding prose punctuation in a bare URL. Balanced
  // parentheses and IPv6 brackets are part of the address when present.
  const std::string_view punctuation =
      reinterpret_cast<const char*>(u8"，。；：！？、（）【】《》「」『』“”‘’");
  int parentheses = 0, brackets = 0;
  size_t end = start;
  for (; end < text.size(); ++end) {
    const unsigned char ch = text[end];
    if (ch <= 0x20 || ch == 0x7f ||
        std::string_view("<>\"'`\\{}").find(ch) != std::string_view::npos ||
        (ch >= 0xe0 && end + 3 <= text.size() &&
         punctuation.find(text.substr(end, 3)) != std::string_view::npos)) {
      break;
    }
    if (ch == '(') ++parentheses;
    if (ch == ')' && parentheses-- == 0) break;
    if (ch == '[') ++brackets;
    if (ch == ']' && brackets-- == 0) break;
  }
  return end;
}

inline void AppendLiteral(std::string& output, std::string_view text) {
  for (const unsigned char ch : text) {
    // Entity-encode spaces so leading indentation is not parsed as code.
    if (ch == ' ') {
      output += "&#32;";
    } else if (ch == '\t') {
      output += "&#9;";
    } else {
      if (ch < 0x80 && std::ispunct(ch)) output += '\\';
      output += static_cast<char>(ch);
    }
  }
}

inline void AppendLink(std::string& output, std::string_view label,
                       std::string_view url) {
  output += '[';
  AppendLiteral(output, label);
  output += "](<";
  for (const char ch : url) {
    // Markdown decodes entities in link destinations; keep query strings exact.
    if (ch == '&')
      output += "&amp;";
    else
      output += ch;
  }
  output += ">)";
}

}  // namespace detail

inline slint::StyledText Format(std::string_view body) {
  std::string markdown;
  markdown.reserve(body.size());
  bool has_link = false;
  for (size_t pos = 0; pos < body.size();) {
    if (body[pos] == '[') {
      const size_t label_end = body.find("](", pos + 1);
      if (label_end != std::string_view::npos && label_end > pos + 1 &&
          body.substr(pos + 1, label_end - pos - 1).find_first_of("[]\r\n") ==
              std::string_view::npos &&
          WebSchemeLength(body.substr(label_end + 2))) {
        const size_t end = detail::UrlEnd(body, label_end + 2);
        const auto url = body.substr(label_end + 2, end - label_end - 2);
        if (end < body.size() && body[end] == ')' && IsWebUrl(url)) {
          detail::AppendLink(markdown,
                             body.substr(pos + 1, label_end - pos - 1), url);
          pos = end + 1;
          has_link = true;
          continue;
        }
      }
    }
    if (WebSchemeLength(body.substr(pos))) {
      size_t end = detail::UrlEnd(body, pos);
      while (end > pos && std::string_view(".,;:!?").find(body[end - 1]) !=
                              std::string_view::npos) {
        --end;
      }
      const auto url = body.substr(pos, end - pos);
      if (IsWebUrl(url)) {
        detail::AppendLink(markdown, url, url);
        pos = end;
        has_link = true;
        continue;
      }
    }
    if (body[pos] == '\r' && pos + 1 < body.size() && body[pos + 1] == '\n') {
      ++pos;
      continue;
    }
    if (body[pos] == '\n') {
      // Preserve empty lines instead of letting Markdown collapse them.
      if (markdown.empty() || markdown.back() == '\n') markdown += "&#32;";
      markdown += '\n';
    } else {
      detail::AppendLiteral(markdown, body.substr(pos, 1));
    }
    ++pos;
  }
  if (has_link) {
    if (markdown.back() == '\n') markdown += "&#32;";
    if (const auto styled = slint::StyledText::from_markdown(markdown))
      return *styled;
  }
  return slint::StyledText::from_plain_text(body);
}

}  // namespace crossdesk::announcement_text

#endif