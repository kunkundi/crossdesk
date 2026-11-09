#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace crossdesk {

// This directory is deliberately separate from every user's GUI profile.
std::filesystem::path UnattendedDataPath();
bool IsUnattendedEnabled();
bool IsSharedIdentityServiceConfigured();
bool SetUnattendedEnabled(bool enabled);
bool IsLocalSystemProcess();
bool IsAdministratorProcess();
// Reject reparse points, untrusted owners and permissive existing DACLs.
bool PrepareUnattendedDirectory(bool create);
// SYSTEM repairs the old host's user-only root ACL before GUI migration.
bool PrepareUnattendedDirectoryForService();
namespace unattended_detail {
// Parameterized only for isolated native ACL tests; production always uses
// the fixed ProgramData path above.
bool PreparePrivateDirectory(const std::filesystem::path& root, bool create);
// Both owner and DACL are restricted to SYSTEM/Administrators, including
// WRITE_DAC access from a filtered token belonging to the file's creator.
HANDLE OpenPrivateMachineFile(const std::filesystem::path& root,
    const std::filesystem::path& path, DWORD access, DWORD sharing,
    DWORD creation, DWORD flags = FILE_ATTRIBUTE_NORMAL);
// Write through a protected temporary file, then atomically replace the target.
bool WritePrivateMachineFile(const std::filesystem::path& root,
    const std::filesystem::path& path, const std::string& contents);
}  // namespace unattended_detail
bool WriteUnattendedStatus(const std::string& identity, bool online,
                           DWORD session_id);
std::string ReadUnattendedStatus();

}  // namespace crossdesk
