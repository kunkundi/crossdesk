#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace crossdesk {

// This directory is deliberately separate from every user's GUI profile.
std::filesystem::path UnattendedDataPath();
bool IsUnattendedEnabled();
bool SetUnattendedEnabled(bool enabled);
bool IsLocalSystemProcess();
bool IsAdministratorProcess();
// Reject reparse points, untrusted owners and permissive existing DACLs.
bool PrepareUnattendedDirectory(bool create);
namespace unattended_detail {
// Parameterized only for isolated native ACL tests; production always uses
// the fixed ProgramData path above.
bool PreparePrivateDirectory(const std::filesystem::path& root, bool create);
}  // namespace unattended_detail
bool WriteUnattendedStatus(const std::string& identity, bool online,
                           DWORD session_id);
std::string ReadUnattendedStatus();

}  // namespace crossdesk
