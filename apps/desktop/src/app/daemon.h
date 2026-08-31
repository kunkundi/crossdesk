/*
 * @Author: DI JUNKUN
 * @Date: 2025-11-19
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DAEMON_H_
#define _DAEMON_H_

#include <atomic>
#include <functional>
#include <string>

class Daemon {
 public:
  using MainLoopFunc = std::function<void()>;

  Daemon(const std::string& name);

  bool start(MainLoopFunc loop);

  void stop();

  bool isRunning() const;

 private:
  std::string name_;
  bool runWithRestart(MainLoopFunc loop);

  std::atomic<bool> running_;
};

#endif
