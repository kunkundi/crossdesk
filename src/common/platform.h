/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-18
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <iostream>

namespace crossdesk {

std::string GetMac();
std::string GetHostName();
bool IsWaylandSession();

}  // namespace crossdesk
#endif
