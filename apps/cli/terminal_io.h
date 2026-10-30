/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-30
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _TERMINAL_IO_H_
#define _TERMINAL_IO_H_

#include <atomic>
#include <csignal>
#include <cstddef>
#include <memory>
#include <string>

namespace crossdesk::cli {
static_assert(std::atomic<int>::is_always_lock_free);
extern std::atomic<int> interrupted;
void InitializeIo();
const char *Platform();
bool IsTty(int fd);
std::ptrdiff_t Read(int fd, void *buffer, size_t size);
std::ptrdiff_t Write(int fd, const void *buffer, size_t size);
void Close(int fd);
int Duplicate(int fd);
int Redirect(int from, int to);
int OpenLog(const std::string &path);
bool WaitReadable(int fd, int timeout_ms);
void WaitIo(int input_fd, int output_fd, int timeout_ms);
std::string Password();
std::string Logs();
struct TerminalSize {
  int columns = 80, rows = 25, pixel_width = 0, pixel_height = 0;
};
TerminalSize GetTerminalSize(int fd);

class NonBlocking {
public:
  explicit NonBlocking(int fd, bool output = false);
  ~NonBlocking();
  NonBlocking(const NonBlocking &) = delete;
  NonBlocking &operator=(const NonBlocking &) = delete;

private:
  struct State;
  std::unique_ptr<State> state_;
};

class RawTerminal {
public:
  RawTerminal(int input, int output);
  ~RawTerminal();
  RawTerminal(const RawTerminal &) = delete;
  RawTerminal &operator=(const RawTerminal &) = delete;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace crossdesk::cli

#endif
