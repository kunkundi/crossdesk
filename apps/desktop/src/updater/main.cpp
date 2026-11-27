#include "updater/update_core.h"
#include "updater/update_platform.h"
#include "platform/single_instance.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")
#endif

namespace crossdesk::updater {
namespace {
struct Request {
  fs::path application;
  Artifact artifact;
  std::vector<std::string> restart_arguments;
};
Request ReadRequest(const fs::path& job) {
  const auto request = nlohmann::json::parse(ReadText(job / "request.json"));
  const auto application = fs::u8path(request.at("application").get<std::string>());
  if (!SupportedInstallation(application)) throw std::runtime_error("Invalid installation destination");
  const auto artifact = VerifyEnvelope(request.at("envelope").get<std::string>(), PublicKey(),
                                       PlatformKey(application), BuildVersion());
  return {application, artifact,
          request.value("restart_arguments", std::vector<std::string>{})};
}

void WaitForCommit(const fs::path& job, const char* marker = "commit") {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  while (true) {
    if (fs::exists(job / "cancel"))
      throw std::runtime_error("Update cancelled before application exit");
    if (fs::exists(job / marker)) return;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("Application did not confirm update installation");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

int Install(const fs::path& job) {
  if (!IsPrivileged()) return 2;
  fs::path stage;
  int result = 1;
  try {
    const auto request = ReadRequest(job);
    stage = PrivateDirectory();
    const auto package = stage / request.artifact.filename;
#ifndef _WIN32
    std::optional<MaintenanceLock> maintenance;
#endif
    ExecuteInstall({
        [&] {
          // A privileged installer never executes the caller's writable file.
          // Copy into private storage, then verify the exact copy being installed.
          CopyPackage(job / request.artifact.filename, package, request.artifact);
        },
        [&] {
#ifndef _WIN32
          // Authorization and staging finish while the GUI can still service
          // the remote connection. An empty directory publishes readiness
          // atomically without writing through a caller-controlled file.
          if (fs::exists(job / "cancel"))
            throw std::runtime_error("Update cancelled before application exit");
          maintenance.emplace(request.application);
          if (!fs::create_directory(job / "ready"))
            throw std::runtime_error("Update readiness was already published");
          WaitForCommit(job, "coordinator-ready");
          if (!fs::exists(job / "commit"))
            throw std::runtime_error("Application did not commit the update");
#endif
          WaitForApplicationExit(request.application);
        },
        [&] { InstallPackage(package, request.application); }});
    result = 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  }
  RestoreServices();
  if (!stage.empty()) {
    std::error_code ignored;
    fs::remove_all(stage, ignored);
  }
  return result;
}

int Coordinate(const fs::path& job) {
  fs::path application;
  std::vector<std::string> restart_arguments;
  bool committed = false;
  bool success = false;
  bool exited = false;
  std::string error;
  platform::SingleInstanceGuard client;
  platform::SingleInstanceGuard launcher;
#ifdef _WIN32
  std::unique_ptr<MaintenanceLock> maintenance;
#else
  std::future<int> elevated;
#endif
  try {
    const auto request = ReadRequest(job);
    application = request.application;
    restart_arguments = request.restart_arguments;
    auto pinned = PinHelper(ExecutablePath());
    {
#ifdef _WIN32
      maintenance = std::make_unique<MaintenanceLock>(application);
#endif
      VerifyPackage(job / request.artifact.filename, request.artifact);
#ifdef _WIN32
      WriteText(job / "ready", "ready\n");
      WaitForCommit(job);
      committed = true;
#else
      // The root-owned helper publishes ready only after authorization and
      // verification of its private package copy, then waits for the GUI's
      // commit before touching the installation.
      elevated = std::async(std::launch::async, RunElevated, HelperPath(application), job);
      while (!fs::exists(job / "commit")) {
        if (elevated.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready)
          throw std::runtime_error("Update preparation failed or authorization was cancelled (code " +
                                   std::to_string(elevated.get()) + ")");
      }
      committed = true;
#endif
      const auto exit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
      while (true) {
        std::string lease_error;
        const auto client_result = client.TryAcquire(platform::InstanceRole::kClient, lease_error);
        const auto launcher_result = launcher.TryAcquire(platform::InstanceRole::kLauncher, lease_error);
        if (client_result == platform::InstanceResult::kError ||
            launcher_result == platform::InstanceResult::kError)
          throw std::runtime_error("Cannot wait for application exit: " + lease_error);
        if (client_result == platform::InstanceResult::kAcquired &&
            launcher_result == platform::InstanceResult::kAcquired) break;
        if (std::chrono::steady_clock::now() >= exit_deadline)
          throw std::runtime_error("CrossDesk did not exit; no files were installed");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      exited = true;
#ifdef _WIN32
      // This relocated copy is held read-only across UAC and installation.
      const int code = RunElevated(ExecutablePath(), job);
#else
      // Hold the initiating user's launch guards through the result receipt, even
      // after the privileged installer exits and releases the machine-wide lock.
      WriteText(job / "coordinator-ready", "ready\n");
      const int code = elevated.get();
#endif
      if (code != 0) throw std::runtime_error("Installation failed or authorization was cancelled (code " +
                                              std::to_string(code) + ")");
      success = true;
    }
  } catch (const std::exception& exception) { error = exception.what(); }
#ifndef _WIN32
  if (elevated.valid()) {
    try { WriteText(job / "cancel", "cancel\n"); } catch (const std::exception&) {}
    elevated.wait();
  }
#endif
  try {
    WriteText(job / "result.json", nlohmann::json{{"success", success}, {"error", error}}.dump(2));
  } catch (const std::exception&) {}
  client.Release();
  launcher.Release();
#ifdef _WIN32
  maintenance.reset();
#endif
  if (committed && exited) {
    try {
      Restart(application, restart_arguments);
      if (success) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (!fs::exists(job / "started") && std::chrono::steady_clock::now() < deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!fs::exists(job / "started")) {
          success = false;
          WriteText(job / "result.json", nlohmann::json{{"success", false},
              {"error", "Installation finished but the new application did not confirm startup. Open CrossDesk or run the installer manually."}}.dump(2));
        }
      }
    }
    catch (const std::exception& exception) {
      success = false;
      WriteText(job / "result.json", nlohmann::json{{"success", false},
          {"error", std::string("Could not restart CrossDesk: ") + exception.what()}}.dump(2));
    }
  }
  if (success) {
    // Keep request/result for diagnostics, remove the large download.
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(job)) {
      if (entry.path().extension() == ".pkg" || entry.path().extension() == ".deb" ||
          (entry.path().extension() == ".exe" && entry.path() != ExecutablePath()))
        fs::remove(entry.path(), ignored);
    }
  }
  return success ? 0 : 1;
}
}  // namespace
}  // namespace crossdesk::updater

int main(int argc, char** argv) {
  using namespace crossdesk::updater;
  try {
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) return 2;
    for (int i = 0; i < count; ++i) args.push_back(fs::path(wide[i]).u8string());
    LocalFree(wide);
    (void)argc; (void)argv;
#else
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    if (args.size() != 3) return 2;
    const auto job = fs::u8path(args[2]);
    if (!job.is_absolute() || fs::is_symlink(job) || !fs::is_directory(job)) return 2;
    if (args[1] == "--install") return Install(job);
    if (args[1] == "--coordinate" && OwnedJobDirectory(job)) return Coordinate(job);
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
  return 2;
}
