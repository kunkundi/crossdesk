/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-24
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LAN_IDENTITY_H_
#define _LAN_IDENTITY_H_

#include <openssl/x509.h>

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace crossdesk::lan {
struct Interface {
  std::string address;
  unsigned ipv6_index = 0;
  unsigned prefix_length = 0;
};
struct Target {
  std::string address;
  // Learned from the current connection, never pinned.
  std::string device_id;
  unsigned port = 52888;
};
struct DialEndpoint {
  enum Kind { IpLiteral, DnsName } kind;
  std::string host;
  unsigned port;
};
inline constexpr size_t kMaxDialEndpointSize = 260;
DialEndpoint ParseDialEndpoint(const std::string& text, unsigned default_port);
bool IsDnsName(std::string_view host);
bool ValidFingerprint(const std::string& value);
std::string CertificateFingerprint(X509* certificate);
unsigned ParsePort(const std::string& value);
std::pair<std::string, unsigned> ParseEndpoint(const std::string& value,
                                               unsigned default_port);
bool IsIpAddress(const std::string& value);
std::string NormalizeAddress(const std::string& value);
std::string FormatEndpoint(const std::string& address, unsigned port);
inline constexpr char kAllInterfaces[] = "::";
// Non-loopback unicast IPv4/IPv6 addresses, optionally restricted to physical
// NICs.
std::vector<Interface> LocalIpInterfaces(bool physical_only = false);
std::vector<Interface> LocalMulticastInterfaces();
std::string RandomHex(size_t bytes);
bool ValidPassword(const std::string& password);
bool VerifyLanCertificate(X509* cert);

// Files are separate from cloud settings and thumbnail credentials. A corrupt
// store is an error; it must never be replaced with a newly generated identity.
class Identity {
 public:
  explicit Identity(std::filesystem::path directory);
  void Load();
  static std::pair<std::string, nlohmann::json> GeneratePassword();
  void SetPassword(const std::string& password, const nlohmann::json& verifier);
  std::string ReadPassword() const;
  static nlohmann::json RegisterPassword(const std::string& password);
  static bool CurrentVerifier(const nlohmann::json& verifier);
  static bool VerifyPassword(const nlohmann::json& verifier,
                             const std::string& password);
  void SaveSettings(const std::string& address, unsigned port);
  // An unconfigured address uses the default port; offline addresses keep
  // their saved port. Selection only controls the address shown in the UI.
  unsigned PortFor(const std::string& address) const;
  bool HasPortFor(const std::string& address) const;
  bool listen_all() const { return settings().value("listen_all", true); }
  void SaveListenerMode(bool listen_all,
                        const std::map<std::string, unsigned>& ports);
  void SelectAddress(const std::string& address);
  const std::string& id() const { return id_; }
  std::filesystem::path certificate() const {
    return directory_ / "certificate.pem";
  }
  std::filesystem::path private_key() const {
    return directory_ / "private-key.pem";
  }
  const nlohmann::json& verifier() const { return state_.at("verifier"); }
  const nlohmann::json& settings() const { return state_.at("settings"); }

 private:
  void Save(const nlohmann::json& state);
  std::filesystem::path directory_;
  std::string id_;
  nlohmann::json state_;
};
}  // namespace crossdesk::lan

#endif
