/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SIGNAL_SERVER_SETTINGS_H_
#define _SIGNAL_SERVER_SETTINGS_H_

#include <tuple>

#include "config_center.h"

namespace crossdesk {

// Include the identity namespace as well as the effective endpoint. Editing
// an inactive self-hosted endpoint must not disconnect the public server.
inline auto GetSignalServerSettings(const ConfigCenter& config) {
  const bool self_hosted = config.IsSelfHosted();
  return std::make_tuple(
      self_hosted,
      self_hosted ? config.GetSignalServerHost() : config.GetDefaultServerHost(),
      self_hosted ? config.GetSignalServerPort()
                  : config.GetDefaultSignalServerPort());
}

}  // namespace crossdesk

#endif