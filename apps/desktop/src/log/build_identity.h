/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-15
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _BUILD_IDENTITY_H_
#define _BUILD_IDENTITY_H_

namespace crossdesk {

struct BuildIdentity {
  const char* crossdesk_revision;
  const char* crossdesk_state;
  const char* minirtc_revision;
  const char* minirtc_state;
};

// Immutable build metadata; unavailable source metadata is reported as unknown.
const BuildIdentity& GetBuildIdentity();

}  // namespace crossdesk

#endif
