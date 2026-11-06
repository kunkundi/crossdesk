#include "mac_permission_migration.h"

#import <Foundation/Foundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <Security/Security.h>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

#include "private_file.h"
#include "platform/single_instance.h"

namespace crossdesk {
namespace {
using nlohmann::json;
constexpr const char* kMigrationFile =
    "/Library/Application Support/CrossDesk/permission-migration.json";

bool IsDeveloperIdProcess() {
  SecCodeRef code = nullptr;
  SecRequirementRef requirement = nullptr;
  const auto compiled = SecRequirementCreateWithString(
      CFSTR("anchor apple generic and identifier \"cn.crossdesk.app\" and "
            "certificate 1[field.1.2.840.113635.100.6.2.6] exists and "
            "certificate leaf[field.1.2.840.113635.100.6.1.13] exists"),
      kSecCSDefaultFlags, &requirement);
  const bool valid = compiled == errSecSuccess &&
      SecCodeCopySelf(kSecCSDefaultFlags, &code) == errSecSuccess &&
      SecCodeCheckValidity(code, kSecCSDefaultFlags, requirement) == errSecSuccess;
  if (code) CFRelease(code);
  if (requirement) CFRelease(requirement);
  return valid;
}

bool KnownBundleId(const std::string& value) {
  return value == "cn.crossdesk.app" || value == "cn.crossdesk.CrossDesk" ||
         value == "com.crossdesk.app" || value == "com.crossdesk.CrossDesk" ||
         value == "com.kunkundi.crossdesk" || value == "com.kunkundi.CrossDesk";
}

bool PermissionGranted(const std::string& service) {
  if (service == "Accessibility") return AXIsProcessTrusted();
  return CGPreflightScreenCaptureAccess();
}

bool ResetPermission(const std::string& service, const std::string& bundle_id) {
  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/tccutil"];
  task.arguments = @[@"reset", @(service.c_str()), @(bundle_id.c_str())];
  task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
  task.standardError = [NSFileHandle fileHandleWithNullDevice];
  NSError* error = nil;
  if (![task launchAndReturnError:&error]) return false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (task.running && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (task.running) {
    kill(task.processIdentifier, SIGKILL);
    [task waitUntilExit];
    return false;
  }
  [task waitUntilExit];
  return task.terminationReason == NSTaskTerminationReasonExit &&
         task.terminationStatus == 0;
}

MacPermissionMigrationResult Migrate(bool retry) {
  if (!IsDeveloperIdProcess()) return {};
  struct stat info {};
  if (lstat(kMigrationFile, &info) != 0) {
    if (errno == ENOENT) return {};
    throw std::runtime_error("Cannot read the permission migration marker");
  }
  if (!S_ISREG(info.st_mode) || info.st_uid != 0 ||
      (info.st_mode & 0022) != 0 || info.st_size > 16384) {
    throw std::runtime_error("Invalid permission migration marker ownership or size");
  }
  std::ifstream input(kMigrationFile);
  const json migration = json::parse(input);
  const std::string generation = migration.at("migration_id");
  const std::string bundle_id = migration.at("legacy_bundle_id");
  const std::string app_path = migration.at("app_path");
  NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:@(generation.c_str())];
  if (migration.at("schema") != 1 || !uuid || !KnownBundleId(bundle_id)) {
    throw std::runtime_error("Invalid permission migration marker contents");
  }
  if (![NSBundle.mainBundle.bundlePath isEqualToString:@(app_path.c_str())]) return {};
  if (geteuid() == 0) {
    throw std::runtime_error("Run CrossDesk as the logged-in user to migrate permissions");
  }

  platform::SingleInstanceGuard guard("CrossDeskPermissionMigration");
  std::string lock_error;
  if (guard.TryAcquire(platform::InstanceRole::kClient, lock_error) !=
      platform::InstanceResult::kAcquired) {
    throw std::runtime_error("Permission migration is already running or its lock is unavailable");
  }
  NSURL* support = [NSFileManager.defaultManager
      URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask
      appropriateForURL:nil create:YES error:nullptr];
  if (!support) throw std::runtime_error("Cannot find the user's application support directory");
  const std::filesystem::path directory =
      std::filesystem::path(support.fileSystemRepresentation) / "CrossDesk" / "PermissionMigration";
  private_file::EnsureDirectory(directory, true);
  const auto state_file = directory / "progress.json";
  json state = {{"schema", 1}, {"migration_id", generation}, {"services", json::object()}};
  if (std::filesystem::exists(state_file)) {
    state = json::parse(private_file::Read(state_file));
    if (state.at("schema") != 1 || !state.at("services").is_object()) {
      throw std::runtime_error("Invalid per-user permission migration progress");
    }
    if (state.at("migration_id") != generation) {
      state = {{"schema", 1}, {"migration_id", generation}, {"services", json::object()}};
    }
  }

  MacPermissionMigrationResult result;
  for (const std::string service : {"Accessibility", "ScreenCapture"}) {
    const std::string progress = state["services"].value(service, "pending");
    if (progress == "done") continue;
    if (PermissionGranted(service)) {
      // The user may have fixed this manually after a failed/interrupted reset.
      state["services"][service] = "done";
      private_file::Write(state_file, state.dump());
      continue;
    }
    if (!retry && progress != "pending") {
      result.needs_repair = true;
      result.detail += service + " needs a manual retry. ";
      continue;
    }
    // Persist intent before the external operation. An interrupted attempt is
    // offered for explicit retry, never silently repeated at the next launch.
    state["services"][service] = "attempted";
    private_file::Write(state_file, state.dump());
    if (ResetPermission(service, bundle_id)) {
      state["services"][service] = "done";
    } else {
      state["services"][service] = "failed";
      result.needs_repair = true;
      result.detail += service + " reset failed. ";
    }
    private_file::Write(state_file, state.dump());
  }
  return result;
}
}  // namespace

MacPermissionMigrationResult MigrateMacPermissions(bool retry) {
  @autoreleasepool {
    @try {
      try {
        return Migrate(retry);
      } catch (const std::exception& error) {
        return {true, error.what()};
      }
    } @catch (NSException* error) {
      return {true, error.reason.UTF8String ?: "Permission migration failed"};
    }
  }
}
}  // namespace crossdesk
