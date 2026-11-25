#include "platform/autostart_backend.h"

#import <Foundation/Foundation.h>

#include <limits.h>
#include <mach-o/dyld.h>

#include <cstdlib>
#include <filesystem>

namespace crossdesk::platform {
namespace {

std::filesystem::path AutostartPath(const std::string& app_name) {
  const char* home = std::getenv("HOME");
  if (!home || !home[0]) return {};
  return std::filesystem::path(home) / "Library" / "LaunchAgents" /
         (app_name + ".plist");
}

bool ConfigureLaunchAgent(const std::string& app_name,
                          const std::string& executable_path, bool create) {
  const std::filesystem::path path = AutostartPath(app_name);
  if (path.empty()) return false;

  std::error_code error;
  @autoreleasepool {
    NSString* label = @(app_name.c_str());
    NSString* executable = @(executable_path.c_str());
    NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
    NSMutableDictionary* agent = [NSMutableDictionary dictionary];
    NSArray* arguments = @[executable];
    if (std::filesystem::exists(path, error)) {
      // Add the marker to our existing job without losing launchd overrides.
      NSDictionary* existing = [NSDictionary dictionaryWithContentsOfURL:url error:nullptr];
      if (!existing) return false;
      arguments = existing[@"ProgramArguments"];
      if (![existing[@"Label"] isEqual:label] || ![arguments isKindOfClass:NSArray.class] ||
          ![arguments.firstObject isEqual:executable]) return !create;
      if ([arguments containsObject:@"--autostart"] || (!create && arguments.count != 1)) return true;
      [agent addEntriesFromDictionary:existing];
    } else {
      if (error) return false;
      if (!create) return true;
      std::filesystem::create_directories(path.parent_path(), error);
      if (error) return false;
      agent[@"Label"] = label;
      agent[@"RunAtLoad"] = @YES;
    }
    agent[@"ProgramArguments"] = [arguments arrayByAddingObject:@"--autostart"];
    NSData* plist = [NSPropertyListSerialization
        dataWithPropertyList:agent format:NSPropertyListXMLFormat_v1_0
                     options:0 error:nullptr];
    return [plist writeToURL:url options:NSDataWritingAtomic error:nullptr];
  }
}

}  // namespace

std::string GetAutostartExecutablePath() {
  char path[PATH_MAX] = {};
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) != 0) return {};
  char resolved[PATH_MAX] = {};
  return realpath(path, resolved) ? std::string(resolved) : std::string(path);
}

bool EnableAutostart(const std::string& app_name,
                     const std::string& executable_path) {
  return ConfigureLaunchAgent(app_name, executable_path, true);
}

bool MigrateLegacyAutostart(const std::string& app_name) {
  const auto executable = GetAutostartExecutablePath();
  return !executable.empty() && ConfigureLaunchAgent(app_name, executable, false);
}

bool DisableAutostart(const std::string& app_name) {
  const std::filesystem::path path = AutostartPath(app_name);
  if (path.empty()) return false;
  std::error_code error;
  return std::filesystem::remove(path, error) && !error;
}

bool IsAutostartEnabled(const std::string& app_name) {
  const std::filesystem::path path = AutostartPath(app_name);
  return !path.empty() && std::filesystem::is_regular_file(path);
}

}  // namespace crossdesk::platform
