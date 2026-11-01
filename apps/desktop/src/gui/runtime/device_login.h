/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-02
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_LOGIN_H_
#define _DEVICE_LOGIN_H_

#include <nlohmann/json.hpp>
#include <string>

#include "minirtc.h"

namespace crossdesk {
class TrustedDevices;

// One signaling connection's application authentication, owned by its callbacks.
class DeviceLogin {
 public:
  int Exchange(SignalExchangeStage stage, const nlohmann::json& message,
               const TrustedDevices* identity, char* response, size_t capacity);
  bool Pending() const { return phase_ != Phase::Idle; }
  bool UsedPassword() const { return used_password_; }

 private:
  enum class Phase { Idle, AwaitingChallenge, AwaitingResult };
  nlohmann::json request_;
  std::string password_;
  Phase phase_ = Phase::Idle;
  bool used_password_ = false;
};
}  // namespace crossdesk

#endif
