/*
 * @Author: DI JUNKUN
 * @Date: 2025-11-11
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VERSION_CHECKER_H_
#define _VERSION_CHECKER_H_

#include <nlohmann/json.hpp>
#include <string>

namespace crossdesk {

nlohmann::json CheckUpdate();

bool IsNewerVersion(const std::string& current, const std::string& latest);

// Pass latest_patch < 0 when patch metadata is unavailable.
bool IsNewerVersionWithMetadata(const std::string& current,
                                const std::string& latest,
                                const std::string& latest_date,
                                int latest_patch);

}  // namespace crossdesk

#endif
