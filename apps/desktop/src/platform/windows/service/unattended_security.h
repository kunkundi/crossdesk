/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-26
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _UNATTENDED_SECURITY_H_
#define _UNATTENDED_SECURITY_H_

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace crossdesk {

bool IsLocalSystemProcess();
bool IsAdministratorProcess();

namespace unattended_detail {

struct SecurityPolicy {
  std::wstring directory_sddl;
  std::wstring system_file_sddl;
  std::wstring admin_file_sddl;
  std::vector<std::wstring> trusted_sids;
};

const SecurityPolicy& DefaultMachineSecurityPolicy();

// Parameterized only for isolated native ACL tests; production always uses
// the fixed ProgramData path and DefaultMachineSecurityPolicy().
bool PreparePrivateDirectory(const std::filesystem::path& root, bool create,
                             const SecurityPolicy& policy,
                             std::string* diagnostic = nullptr);
bool PreparePrivateDirectory(const std::filesystem::path& root, bool create,
                             std::string* diagnostic = nullptr);

// Repairs a permissive or incomplete DACL only when the target and its parent
// are real directories (never reparse points) and the target is already owned
// by a trusted principal in policy.trusted_sids.
bool RepairPrivateDirectory(const std::filesystem::path& root, bool create,
                            const SecurityPolicy& policy,
                            std::string* diagnostic = nullptr,
                            bool* repaired = nullptr);
bool RepairPrivateDirectory(const std::filesystem::path& root, bool create,
                            std::string* diagnostic = nullptr,
                            bool* repaired = nullptr);

// Both owner and DACL are restricted to policy.trusted_sids, including
// WRITE_DAC access from a filtered token belonging to the file's creator.
HANDLE OpenPrivateMachineFile(const std::filesystem::path& root,
                              const std::filesystem::path& path, DWORD access,
                              DWORD sharing, DWORD creation,
                              DWORD flags, const SecurityPolicy& policy,
                              std::string* diagnostic = nullptr);
HANDLE OpenPrivateMachineFile(const std::filesystem::path& root,
                              const std::filesystem::path& path, DWORD access,
                              DWORD sharing, DWORD creation,
                              DWORD flags = FILE_ATTRIBUTE_NORMAL,
                              std::string* diagnostic = nullptr);

// Write through a protected temporary file, then atomically replace the target.
bool WritePrivateMachineFile(const std::filesystem::path& root,
                             const std::filesystem::path& path,
                             const std::string& contents,
                             const SecurityPolicy& policy);
bool WritePrivateMachineFile(const std::filesystem::path& root,
                             const std::filesystem::path& path,
                             const std::string& contents);

}  // namespace unattended_detail
}  // namespace crossdesk

#endif
