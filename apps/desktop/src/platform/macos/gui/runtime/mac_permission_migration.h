/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MAC_PERMISSION_MIGRATION_H_
#define _MAC_PERMISSION_MIGRATION_H_

#include <string>

namespace crossdesk {

struct MacPermissionMigrationResult {
  bool needs_repair = false;
  std::string detail;
};

// Runs on a worker before the new app requests missing permissions. Explicit
// retries may resume failed/interrupted services, while completed ones persist.
MacPermissionMigrationResult MigrateMacPermissions(bool retry);

}  // namespace crossdesk

#endif
