/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-13
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _MOBILE_DEVICE_TRUST_H_
#define _MOBILE_DEVICE_TRUST_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace crossdesk::mobile_controller {
// The platform must durably save the seed in its secure store before using it.
// Existing invalid/unavailable identities must never be replaced automatically.
class DeviceTrustIdentity {
 public:
  explicit DeviceTrustIdentity(std::string_view seed);
  ~DeviceTrustIdentity();
  static std::string GeneratePrivateKey();
  static void ClearSecret(std::string& value);
  static std::string Advertise(std::string_view host_information);
  static bool IsChallenge(std::string_view data) noexcept;
  std::string PairingInfo(const std::string& device_id, const std::string& name) const;
  const std::string& Fingerprint() const;
  // nullopt: ordinary control message; empty: unsupported/malformed challenge.
  std::optional<std::string> ChallengeResponse(std::string_view data,
      const std::string& device_id, const std::string& name) const noexcept;
 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace crossdesk::mobile_controller

#endif
