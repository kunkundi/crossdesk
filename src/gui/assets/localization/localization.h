/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */
#ifndef _LOCALIZATION_H_
#define _LOCALIZATION_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "localization_data.h"

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
      {"zh-CN", reinterpret_cast<const char*>(u8"中文")},
      {"en-US", "English"},
      {"ru-RU", reinterpret_cast<const char*>(u8"Русский")}};
  return kSupportedLanguages;
}

namespace detail {

inline int ClampLanguageIndex(int language_index) {
  if (language_index >= 0 &&
      language_index < static_cast<int>(GetSupportedLanguages().size())) {
    return language_index;
  }
  return 0;
}

using TranslationTable =
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>;

inline std::unordered_map<std::string, std::string> MakeLocalizedValues(
    const TranslationRow& row) {
  return {{"zh-CN", reinterpret_cast<const char*>(row.zh)},
          {"en-US", reinterpret_cast<const char*>(row.en)},
          {"ru-RU", reinterpret_cast<const char*>(row.ru)}};
}

inline TranslationTable BuildTranslationTable() {
  TranslationTable table;
  for (const auto& row : kTranslationRows) {
    table[row.key] = MakeLocalizedValues(row);
  }

  return table;
}

inline const TranslationTable& GetTranslationTable() {
  static const TranslationTable table = BuildTranslationTable();
  return table;
}

inline const std::string& GetTranslatedText(const std::string& key,
                                            int language_index) {
  static const std::string kEmptyText = "";

  const auto& table = GetTranslationTable();
  const auto key_it = table.find(key);
  if (key_it == table.end()) {
    return kEmptyText;
  }

  const auto& localized_values = key_it->second;
  const std::string& language_code =
      GetSupportedLanguages()[ClampLanguageIndex(language_index)].code;

  const auto exact_it = localized_values.find(language_code);
  if (exact_it != localized_values.end()) {
    return exact_it->second;
  }

  const auto english_it = localized_values.find("en-US");
  if (english_it != localized_values.end()) {
    return english_it->second;
  }

  const auto chinese_it = localized_values.find("zh-CN");
  if (chinese_it != localized_values.end()) {
    return chinese_it->second;
  }

  return kEmptyText;
}

}  // namespace detail

inline const std::string& LocalizedString::operator[](
    int language_index) const {
  return detail::GetTranslatedText(key_, language_index);
}

#define CROSSDESK_DECLARE_LOCALIZED_STRING(name, zh, en, ru) \
  inline const LocalizedString name(#name);
CROSSDESK_LOCALIZATION_ALL(CROSSDESK_DECLARE_LOCALIZED_STRING)
#undef CROSSDESK_DECLARE_LOCALIZED_STRING

#if _WIN32
inline const wchar_t* GetExitProgramLabel(int language_index) {
  static std::vector<std::wstring> cache(GetSupportedLanguages().size());
  const int normalized_index = detail::ClampLanguageIndex(language_index);
  std::wstring& cached_text = cache[normalized_index];
  if (!cached_text.empty()) {
    return cached_text.c_str();
  }

  const std::string& utf8_text =
      detail::GetTranslatedText("exit_program", normalized_index);
  if (utf8_text.empty()) {
    cached_text = L"Exit";
    return cached_text.c_str();
  }

  int wide_length =
      MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1, nullptr, 0);
  if (wide_length <= 0) {
    cached_text = L"Exit";
    return cached_text.c_str();
  }

  cached_text.resize(static_cast<size_t>(wide_length - 1));
  MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1, cached_text.data(),
                      wide_length);
  return cached_text.c_str();
}
#endif

}  // namespace localization
}  // namespace crossdesk

#endif
