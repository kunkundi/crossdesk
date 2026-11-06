/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-06
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONTROLLER_FILES_H_
#define _CONTROLLER_FILES_H_

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace crossdesk {
class ControllerFiles {
public:
  using Send = std::function<bool(const char *, const std::string &)>;
  explicit ControllerFiles(Send send);
  ~ControllerFiles();
  nlohmann::json Command(const nlohmann::json &request);
  void Message(nlohmann::json message);
  void Receive(const char *data, size_t size, bool acknowledgement);
  void Tick();
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace crossdesk

#endif
