#pragma once

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>

#include "unattended_security.h"

namespace crossdesk {

// This directory is deliberately separate from every user's GUI profile.
std::filesystem::path UnattendedDataPath();
std::filesystem::path UnattendedLogPath();
// Missing configuration is a successful disabled result; other read failures
// must not be mistaken for a completed disable operation.
bool QueryUnattendedEnabled(bool& enabled);
// An absent preference predates the machine-wide startup setting.
bool QueryUnattendedPreference(std::optional<bool>& enabled);
bool IsUnattendedEnabled();
bool IsSharedIdentityServiceConfigured();
bool SetUnattendedEnabled(std::optional<bool> enabled);
// Reject reparse points, untrusted owners and permissive existing DACLs.
bool PrepareUnattendedDirectory(bool create, std::string* error = nullptr);
// Repair a SYSTEM/Administrators-owned unattended directory DACL on a verified
// non-reparse handle before validating private access.
bool RepairUnattendedDirectory(bool create, std::string* error = nullptr,
                               bool* repaired = nullptr);
// SYSTEM repairs the unattended root ACL before starting or migrating hosts.
bool PrepareUnattendedDirectoryForService(std::string* error = nullptr,
                                          bool* repaired = nullptr);
bool WriteUnattendedStatus(const std::string& identity, bool online,
                           DWORD session_id, std::string* diagnostic = nullptr);
std::string ReadUnattendedStatus();

}  // namespace crossdesk
