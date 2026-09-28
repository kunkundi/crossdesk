/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-28
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _APP_VERSION_H_
#define _APP_VERSION_H_

#include <optional>
#include <string>
#include <string_view>

namespace crossdesk {

struct VersionInfo {
  std::string version;
  std::string release_name;
  std::string release_notes;
  std::string release_date;
  int patch = -1;
};

// Shared desktop release parsing and comparison for desktop and iOS controllers.
std::optional<VersionInfo> ParseVersionInfoJSON(std::string_view json);
bool IsValidAppVersion(const std::string& version);

bool IsNewerVersion(const std::string& current, const std::string& latest);

// Pass latest_patch < 0 when patch metadata is unavailable.
bool IsNewerVersionWithMetadata(const std::string& current,
                                const std::string& latest,
                                const std::string& latest_date,
                                int latest_patch);

// Returns the latest release label when the peer is older. Missing or invalid
// peer versions are treated as legacy; the latest release must still be valid.
std::optional<std::string> AvailableAppUpdate(const std::string& current,
                                             const VersionInfo& latest);

}  // namespace crossdesk

#endif