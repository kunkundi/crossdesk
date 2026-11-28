/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */
#ifndef _LOCALIZATION_H_
#define _LOCALIZATION_H_

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "localization_data.h"
#include "system_language.h"

#if _WIN32
#include <Windows.h>
#endif

namespace crossdesk {
namespace localization {

struct LanguageOption {
  std::string code;
  std::string display_name;
};

class LocalizedString {
 public:
  constexpr explicit LocalizedString(const char* key) : key_(key) {}
  const std::string& operator[](int language_index) const;

 private:
  const char* key_;
};

inline const std::vector<LanguageOption>& GetSupportedLanguages() {
  static const std::vector<LanguageOption> kSupportedLanguages = {
      {"zh-CN", "简体中文"}, {"en-US", "English"}, {"ru-RU", "Русский"},
      {"zh-Hant", "繁體中文"}, {"ja", "日本語"}, {"ko", "한국어"},
      {"de", "Deutsch"}, {"fr", "Français"}, {"es", "Español"},
      {"pt-BR", "Português (Brasil)"}, {"tr", "Türkçe"}, {"he", "עברית"}};
  return kSupportedLanguages;
}

namespace detail {

inline int ClampLanguageIndex(int language_index) {
  if (language_index == kSystemLanguage) return SystemLanguage();
  return language_index >= 0 && language_index < kSystemLanguage
             ? language_index : kEnglishLanguage;
}

using TranslationTable = std::unordered_map<std::string, std::vector<std::string>>;
inline const TranslationTable& LiteralTable() {
  static const TranslationTable table = [] {
    TranslationTable result;
    for (const auto& row : kTranslationRows)
      result.emplace(row.key, std::vector<std::string>(std::begin(row.text), std::end(row.text)));
    return result;
  }();
  return table;
}
inline const std::string& GetLiteralText(const std::string& key, int language) {
  static const std::string empty;
  const auto it = LiteralTable().find(key);
  if (it == LiteralTable().end()) return empty;
  const auto& values = it->second;
  return values[ClampLanguageIndex(language)];
}
inline const std::string& GetTranslatedText(const std::string& key, int language) {
  static const std::unordered_map<std::string, std::string> names = [] {
    std::unordered_map<std::string, std::string> result;
    for (const auto& row : kNamedTranslations) result.emplace(row.name, row.literal);
    return result;
  }();
  static const std::string empty;
  const auto it = names.find(key);
  return it == names.end() ? empty : GetLiteralText(it->second, language);
}

}  // namespace detail

inline const std::string& Literal(const std::string& key, int language) {
  return detail::GetLiteralText(key, language);
}

inline const std::string& LocalizedString::operator[](
    int language_index) const {
  return detail::GetTranslatedText(key_, language_index);
}

inline std::string FormatDisplayLabel(size_t display_index,
                                      const std::string& display_name,
                                      int language_index) {
  const std::string number = std::to_string(display_index + 1);
  std::string label =
      detail::GetTranslatedText("display_screen", language_index) + " " +
      number;
  const std::string fallback_name = "Display" + number;
  if (!display_name.empty() && display_name != fallback_name) {
    label += " (" + display_name + ")";
  }
  return label;
}

#define CROSSDESK_DECLARE_LOCALIZED_STRING(name) \
  inline const LocalizedString name(#name);
CROSSDESK_LOCALIZATION_ALL(CROSSDESK_DECLARE_LOCALIZED_STRING)
#undef CROSSDESK_DECLARE_LOCALIZED_STRING

#if _WIN32
inline const wchar_t* GetWindowsLocalizedLabel(
    int language_index, const char* key, const wchar_t* fallback,
    std::vector<std::wstring>& cache) {
  const int normalized_index = detail::ClampLanguageIndex(language_index);
  std::wstring& cached_text = cache[normalized_index];
  if (!cached_text.empty()) {
    return cached_text.c_str();
  }

  const std::string& utf8_text =
      detail::GetTranslatedText(key, normalized_index);
  if (utf8_text.empty()) {
    cached_text = fallback;
    return cached_text.c_str();
  }

  int wide_length =
      MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1, nullptr, 0);
  if (wide_length <= 0) {
    cached_text = fallback;
    return cached_text.c_str();
  }

  cached_text.resize(static_cast<size_t>(wide_length));
  if (MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1,
                          cached_text.data(), wide_length) <= 0) {
    cached_text = fallback;
    return cached_text.c_str();
  }
  cached_text.pop_back();
  return cached_text.c_str();
}

inline const wchar_t* GetShowMainWindowLabel(int language_index) {
  static std::vector<std::wstring> cache(GetSupportedLanguages().size());
  return GetWindowsLocalizedLabel(language_index, "show_main_window",
                                  L"Show Main Window", cache);
}

inline const wchar_t* GetSettingsLabel(int language_index) {
  static std::vector<std::wstring> cache(GetSupportedLanguages().size());
  return GetWindowsLocalizedLabel(language_index, "settings", L"Settings",
                                  cache);
}

inline const wchar_t* GetExitProgramLabel(int language_index) {
  static std::vector<std::wstring> cache(GetSupportedLanguages().size());
  return GetWindowsLocalizedLabel(language_index, "exit_program", L"Exit",
                                  cache);
}
#endif

}  // namespace localization
}  // namespace crossdesk

#endif
