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
#include <optional>

namespace crossdesk::device_identity_store {

// Device signing keys live in the persistent trusted-device directory.
// Read the latest document under a process lock, then atomically persist it.
// Throw on corruption, unsafe permissions, lock contention or persistence error.
// Read returns nullopt only when the identity file is absent (legacy migration).
std::optional<nlohmann::json> Read(const std::filesystem::path& directory);
void Update(const std::filesystem::path& directory,
            const std::function<void(nlohmann::json&, bool exists)>& update);

}  // namespace crossdesk::device_identity_store

#endif
