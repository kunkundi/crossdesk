/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DISPLAY_RESOLUTION_PREFERENCES_H_
#define _DISPLAY_RESOLUTION_PREFERENCES_H_

#include <nlohmann/json.hpp>

#include "display_resolution.h"

namespace crossdesk {

inline nlohmann::json DisplayResolutionPreferencesJson(
    const std::vector<DisplayResolutionPreference> &preferences) {
  auto result = nlohmann::json::array();
  for (const auto &preference : preferences)
    result.push_back({{"display_key", preference.display_key},
                      {"width", preference.resolution.width},
                      {"height", preference.resolution.height}});
  return result;
}

inline bool ReadDisplayResolutionPreferences(
    const nlohmann::json &values,
    std::vector<DisplayResolutionPreference> &result) {
  std::vector<DisplayResolutionPreference> parsed;
  if (!values.is_array() || values.size() > kMaxResolutionDisplays)
    return false;
  try {
    for (const auto &value : values) {
      const auto &width = value.at("width");
      const auto &height = value.at("height");
      if (!width.is_number_integer() || !height.is_number_integer() ||
          width < 1 || width > 32768 || height < 1 || height > 32768)
        return false;
      DisplayResolutionPreference preference{
          value.at("display_key").get<std::string>(),
          {width.get<int>(), height.get<int>()}};
      if (!ValidDisplayResolutionPreference(preference) ||
          std::any_of(parsed.begin(), parsed.end(), [&](const auto &existing) {
            return existing.display_key == preference.display_key;
          }))
        return false;
      parsed.push_back(std::move(preference));
    }
  } catch (const nlohmann::json::exception &) {
    return false;
  }
  result = std::move(parsed);
  return true;
}

} // namespace crossdesk

#endif
