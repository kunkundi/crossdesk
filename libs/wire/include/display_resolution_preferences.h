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

inline nlohmann::json DisplayResolutionPreferenceJson(
    const DisplayResolutionPreference &preference) {
  nlohmann::json value{{"display_key", preference.display_key},
                       {"width", preference.resolution.width},
                       {"height", preference.resolution.height}};
  if (preference.pixel_size)
    value["pixel_size"] = {{"width", preference.pixel_size->width},
                           {"height", preference.pixel_size->height}};
  return value;
}

inline bool ReadResolution(const nlohmann::json &value, DisplayResolution &size) {
  const auto &width = value.at("width");
  const auto &height = value.at("height");
  if (!width.is_number_integer() || !height.is_number_integer() ||
      width < 1 || width > 32768 || height < 1 || height > 32768)
    return false;
  size = {width.get<int>(), height.get<int>()};
  return true;
}

inline bool ReadDisplayPixelSize(const nlohmann::json &value,
                                 std::optional<DisplayResolution> &size) {
  const auto it = value.find("pixel_size");
  if (it == value.end()) {
    size.reset();
    return true;
  }
  DisplayResolution pixels;
  if (!ReadResolution(*it, pixels)) return false;
  size = pixels;
  return true;
}

inline nlohmann::json DisplayResolutionPreferencesJson(
    const std::vector<DisplayResolutionPreference> &preferences) {
  auto result = nlohmann::json::array();
  for (const auto &preference : preferences)
    result.push_back(DisplayResolutionPreferenceJson(preference));
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
      DisplayResolutionPreference preference;
      preference.display_key = value.at("display_key").get<std::string>();
      if (!ReadResolution(value, preference.resolution) ||
          !ReadDisplayPixelSize(value, preference.pixel_size) ||
          !ValidDisplayResolutionPreference(preference) ||
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
