/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-01
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _TRUSTED_DEVICES_H_
#define _TRUSTED_DEVICES_H_

#include <openssl/evp.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crossdesk {

class TrustedDevices {
 public:
  struct Device {
    std::string public_key, name, device_id;
    bool operator==(const Device&) const = default;
  };

  TrustedDevices(const std::filesystem::path& directory,
                 const std::filesystem::path& cache);
  std::string Fingerprint() const;
  const std::string& PublicKey() const { return public_key_; }
  // Callers supply a canonical, purpose-specific transcript.
  std::string Sign(std::string_view transcript) const;
  static std::string Fingerprint(const std::string& public_key);
  static std::string Nonce();
  nlohmann::json Prove(const std::string& nonce,
                       const std::string& device_id, const std::string& name) const;
  static std::optional<Device> Verify(const nlohmann::json& proof,
      const std::string& nonce);
  static constexpr size_t kMaxPairingInfoSize = 2048;
  std::string PairingInfo(const std::string& device_id, const std::string& name) const;
  static std::optional<Device> ParsePairingInfo(std::string_view text);
  bool Contains(const std::string& public_key) const;
  std::vector<Device> List() const;
  // Call only after a local add action for a verified peer or valid pairing info.
  void Add(const Device& device);
  void Remove(const std::string& public_key);
  // Replace a complete snapshot from the authenticated machine host.
  // Validation and persistence finish before the in-memory list changes.
  void Replace(const std::vector<Device>& devices);

 private:
  void Save(const std::map<std::string, Device>& devices) const;
  std::filesystem::path path_;
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key_{nullptr, EVP_PKEY_free};
  std::string public_key_;
  mutable std::mutex mutex_;
  std::map<std::string, Device> devices_;
};

}  // namespace crossdesk

#endif
