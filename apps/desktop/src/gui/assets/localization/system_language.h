/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-27
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SYSTEM_LANGUAGE_H_
#define _SYSTEM_LANGUAGE_H_

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace crossdesk {
namespace localization {

inline constexpr int kSystemLanguage = 12;
inline constexpr int kHebrewLanguage = 11;
inline constexpr int kEnglishLanguage = 1;

// Keep 0/1/2 compatible with existing Chinese/English/Russian config files.
inline int MatchLanguage(std::string tag) {
  for (char& c : tag) {
    if (c == '_') c = '-';
    else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  }
  tag = tag.substr(0, tag.find_first_of(".@"));
  const auto base = tag.substr(0, tag.find('-'));
  if (base == "zh") {
    const auto parts = "-" + tag + "-";
    if (parts.find("-hans-") != std::string::npos) return 0;
    return parts.find("-hant-") != std::string::npos ||
                   parts.find("-tw-") != std::string::npos ||
                   parts.find("-hk-") != std::string::npos ||
                   parts.find("-mo-") != std::string::npos ? 3 : 0;
  }
  const char* codes[] = {"zh", "en", "ru", "zh", "ja", "ko", "de", "fr", "es", "pt", "tr", "he"};
  for (int i = 1; i < kSystemLanguage; ++i)
    if (i != 3 && base == codes[i]) return i;
  // iw הוא קוד ה-ISO הישן לעברית, ש-Java ו-glibc ישנים עדיין מחזירים.
  if (base == "iw") return kHebrewLanguage;
  if (base == "c" || base == "posix") return kEnglishLanguage;
  return -1;
}

inline int MatchPreferredLanguages(const std::vector<std::string>& tags) {
  for (const auto& tag : tags) {
    const int match = MatchLanguage(tag);
    if (match >= 0) return match;
  }
  return kEnglishLanguage;
}

inline int DetectSystemLanguage() {
  std::vector<std::string> tags;
#if defined(_WIN32)
  ULONG count = 0, length = 0;
  GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &length);
  std::vector<wchar_t> names(length);
  if (length && GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &length)) {
    for (const wchar_t* name = names.data(); *name; name += wcslen(name) + 1) {
      std::string tag;
      for (const wchar_t* c = name; *c; ++c) tag += static_cast<char>(*c);
      tags.push_back(tag);
    }
  }
#elif defined(__APPLE__)
  if (CFArrayRef names = CFLocaleCopyPreferredLanguages()) {
    for (CFIndex i = 0; i < CFArrayGetCount(names); ++i) {
      char tag[128] = {};
      auto name = static_cast<CFStringRef>(CFArrayGetValueAtIndex(names, i));
      if (CFStringGetCString(name, tag, sizeof(tag), kCFStringEncodingUTF8)) tags.emplace_back(tag);
    }
    CFRelease(names);
  }
#else
  // The Harmony desktop host supplies LANGUAGE from the system i18n API.
  const char* locale = std::getenv("LC_ALL");
  if (!locale || !*locale) locale = std::getenv("LC_MESSAGES");
  if (!locale || !*locale) locale = std::getenv("LANG");
  if (locale && (std::string(locale) == "C" || std::string(locale) == "POSIX")) return kEnglishLanguage;
  if (const char* preferred = std::getenv("LANGUAGE")) {
    std::string list(preferred);
    size_t start = 0;
    do {
      const auto end = list.find(':', start);
      tags.push_back(list.substr(start, end - start));
      if (end == std::string::npos) break;
      start = end + 1;
    } while (start < list.size());
  }
  if (locale) tags.emplace_back(locale);
#endif
  return MatchPreferredLanguages(tags);
}

inline int SystemLanguage() {
  // Language changes take effect on the next application launch; explicit
  // application preferences always win over this process-wide default.
  static const int language = DetectSystemLanguage();
  return language;
}

}  // namespace localization
}  // namespace crossdesk

#endif
