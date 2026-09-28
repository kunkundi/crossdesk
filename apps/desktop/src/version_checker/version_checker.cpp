/*
 * @Author: DI JUNKUN
 * @Date: 2025-11-11
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#include "version_checker.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "rd_log.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace crossdesk {

namespace {

void LogHttpError(const httplib::Result& result) {
  LOG_WARN("Failed to fetch version.json: error={}, message={}",
           static_cast<int>(result.error()), httplib::to_string(result.error()));
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  LOG_WARN("version.json SSL error={}, OpenSSL error={}", result.ssl_error(),
           result.ssl_openssl_error());
#endif
}

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) && defined(__linux__)
bool PathExists(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

std::string GetEnvPathIfExists(const char* key) {
  const char* value = std::getenv(key);
  if (!value) {
    return "";
  }

  const std::string path = value;
  return PathExists(path) ? path : "";
}

std::string FindFirstExistingPath(
    const std::vector<std::string>& candidates) {
  for (const auto& candidate : candidates) {
    if (PathExists(candidate)) {
      return candidate;
    }
  }
  return "";
}

void ConfigureLinuxCaCerts(httplib::Client* cli) {
  const std::string ca_file = [&]() {
    const std::string env_path = GetEnvPathIfExists("SSL_CERT_FILE");
    if (!env_path.empty()) {
      return env_path;
    }

    return FindFirstExistingPath({
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/cert.pem",
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
    });
  }();

  const std::string ca_dir = [&]() {
    const std::string env_path = GetEnvPathIfExists("SSL_CERT_DIR");
    if (!env_path.empty()) {
      return env_path;
    }

    return FindFirstExistingPath({
        "/etc/ssl/certs",
        "/etc/pki/tls/certs",
        "/etc/openssl/certs",
    });
  }();

  if (ca_file.empty() && ca_dir.empty()) {
    LOG_WARN("No Linux CA bundle found for version.json request; relying on OpenSSL defaults");
    return;
  }

  cli->set_ca_cert_path(ca_file, ca_dir);
  LOG_INFO("Configured version.json TLS CA bundle: file={}, dir={}",
           ca_file.empty() ? "<none>" : ca_file,
           ca_dir.empty() ? "<none>" : ca_dir);
}
#endif

}  // namespace

std::optional<VersionInfo> CheckUpdate() {
  httplib::Client cli("https://version.crossdesk.cn");

  cli.set_connection_timeout(5);
  cli.set_read_timeout(5);
  cli.set_write_timeout(5);
  cli.set_max_timeout(15000);
  cli.set_follow_location(true);

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) && defined(__linux__)
  ConfigureLinuxCaCerts(&cli);
#endif

  auto res = cli.Get("/version.json");
  if (res) {
    if (res->status == 200) {
      try {
        auto info = ParseVersionInfo(nlohmann::json::parse(res->body));
        if (info) {
          LOG_INFO("Fetched version.json: latest_version={}, releaseDate={}, patch={}",
                   info->version, info->release_date, info->patch);
        } else {
          LOG_WARN("version.json does not contain a valid version");
        }
        return info;
      } catch (const std::exception& e) {
        LOG_WARN("Failed to parse version.json: {}", e.what());
        return std::nullopt;
      }
    } else {
      LOG_WARN("Failed to fetch version.json: HTTP status={}", res->status);
      return std::nullopt;
    }
  } else {
    LogHttpError(res);
    return std::nullopt;
  }
}

}  // namespace crossdesk
