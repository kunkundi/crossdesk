/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-11
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DIAGNOSTIC_EXPORT_H_
#define _DIAGNOSTIC_EXPORT_H_

#include <ctime>
#include <filesystem>
#include <string>

namespace crossdesk::feedback {

enum class ExportError { none, no_logs, read_failed, write_failed, too_large };
struct ExportResult {
  ExportError error = ExportError::none;
};

// Exports byte-for-byte snapshots of the active application and transport
// logs, plus basic environment information. No account configuration is read.
ExportResult ExportDiagnostics(const std::filesystem::path& application_logs,
                               const std::filesystem::path& transport_logs,
                               const std::filesystem::path& destination,
                               const std::string& version,
                               std::time_t now = std::time(nullptr));
std::string DiagnosticFileName();

}  // namespace crossdesk::feedback

#endif
