/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-15
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOBILE_SESSION_H_
#define _MOBILE_SESSION_H_

#include <string>

namespace crossdesk::harmony {
// Commands execute on one worker. MiniRTC callbacks copy their borrowed data
// before queuing it. Poll is the only bridge to the ArkTS thread.
// Mobile uses an identity-only signaling peer and can never accept a guest.
__attribute__((visibility("default"))) void MobileCommand(const std::string& json);
__attribute__((visibility("default"))) std::string MobilePoll();
__attribute__((visibility("default"))) void MobileShutdown();
__attribute__((visibility("default"))) std::string MobileGenerateTrustKey();
__attribute__((visibility("default"))) std::string MobileTrustInfo(const std::string& seed, const std::string& device_id);
}

#endif
