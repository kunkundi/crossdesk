/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-09
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LOCAL_HOST_STATUS_H_
#define _LOCAL_HOST_STATUS_H_

#include <nlohmann/json.hpp>

namespace crossdesk {

// Pending sections survive deferred settings and failed local persistence.
// Clear each flag only after its section has been applied successfully.
struct LocalHostStatus {
  nlohmann::json values = nlohmann::json::object();
  bool config_dirty = true;
  bool identity_dirty = true;
  bool controllers_dirty = true;
  bool trusted_dirty = true;

  void Reset() {
    values.clear();
    config_dirty = identity_dirty = controllers_dirty = trusted_dirty = true;
  }

  void Merge(const nlohmann::json& update) {
    const auto changed = [&](const char* field) {
      return update.contains(field) &&
          (!values.contains(field) || update.at(field) != values.at(field));
    };
    config_dirty |= changed("config");
    identity_dirty |= changed("identity") || changed("self_hosted");
    controllers_dirty |= changed("controllers");
    trusted_dirty |= changed("trusted");
    values.update(update);
  }
};

}  // namespace crossdesk

#endif