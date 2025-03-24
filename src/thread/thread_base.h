#ifndef _THREAD_BASE_H_
#define _THREAD_BASE_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

class ThreadBase {
 public:
  ThreadBase();
  virtual ~ThreadBase();

 public:
  void Start();
  void Stop();

  void Pause();
  void Resume();

  void SetPeriod(std::chrono::milliseconds period);
  void SetThreadName(const std::string& name);
  std::string GetThreadName();

  virtual bool Process() = 0;

 private:
  void Run();

 private:
  std::thread thread_;
  std::chrono::milliseconds period_;
  std::string thread_name_;

  std::condition_variable cv_;
  std::mutex cv_mtx_;

  std::atomic<bool> running_;
  std::atomic<bool> pause_;
};

#endif