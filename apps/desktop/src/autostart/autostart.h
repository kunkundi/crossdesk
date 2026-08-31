/*
 * @Author: DI JUNKUN
 * @Date: 2025-11-18
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AUTOSTART_H_
#define _AUTOSTART_H_

#include <string>

namespace crossdesk {

bool EnableAutostart(const std::string& appName);

bool DisableAutostart(const std::string& appName);

bool IsAutostartEnabled(const std::string& appName);

}  // namespace crossdesk
#endif