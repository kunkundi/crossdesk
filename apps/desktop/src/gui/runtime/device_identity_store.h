/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-01
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_IDENTITY_STORE_H_
#define _DEVICE_IDENTITY_STORE_H_

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>

namespace crossdesk::device_identity_store {

// The trusted-device pairing key lives in cache/identity.json. Read the latest
// document under a process lock, apply an update, then atomically persist it.
// Throw on corruption, unsafe permissions, lock contention or persistence error.
void Update(const std::filesystem::path& cache,
            const std::function<void(nlohmann::json&)>& update);

}  // namespace crossdesk::device_identity_store

#endif
