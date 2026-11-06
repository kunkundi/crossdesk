/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-24
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _LAN_SESSION_MANAGER_H_
#define _LAN_SESSION_MANAGER_H_

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "lan_auth_limiter.h"
#include "lan_transport.h"
#include "minirtc.h"

namespace crossdesk::lan {
struct Session {
  enum Phase {
    Connecting,
    AwaitAuth,
    Authenticating,
    Preparing,
    AwaitAuthResult,
    AwaitOffer,
    AwaitAnswer,
    Negotiating,
    Connected,
    Closing,
    Closed
  } phase = Connecting;
  Transport::ChannelPtr channel;
  Target target;
  std::string key, session_id, local_id, remote_id, auth_request;
  std::string password, error;
  bool incoming = false;
  uint64_t epoch = 0, receive_seq = 0, send_seq = 0;
  std::chrono::steady_clock::time_point deadline;
  MiniRtcPeerPtr* peer = nullptr;  // Only published/cleared by the manager.
  std::atomic<bool> closing{false}, authorized{false}, ready{false},
      failed{false};
  std::atomic<int> failure_code{-1};
  std::mutex signal_mutex;
  size_t signal_count = 0, signal_bytes = 0;
  std::deque<std::string> signals, pending_receive;
  std::shared_ptr<void> callback_owner;
  std::function<void()> drain_callbacks;
  static int SendSignal(const char* data, size_t size, void* context);
  static void Error(MiniRtcDirectError error, void* context);
  void Status(MiniRtcConnectionStatus status);
};

// All public methods run on the GUI manager thread. Only Session's bounded
// callback entry points are callable by RTC workers.
class SessionManager {
 public:
  using SessionPtr = std::shared_ptr<Session>;
  struct Prepared {
    MiniRtcPeerPtr* peer = nullptr;
    std::shared_ptr<void> callback_owner;
    std::function<void()> drain_callbacks;
  };
  struct Hooks {
    // Called on the manager to take device snapshots; returned work runs off
    // UI.
    std::function<std::function<Prepared()>(const SessionPtr&)> prepare;
    std::function<void(const SessionPtr&)> attached, connected, closed;
    std::function<void(const std::string&)> error;
  };
  SessionManager(std::filesystem::path directory, Hooks hooks);
  ~SessionManager();
  Identity& identity() { return identity_; }
  // Refreshed by Tick; UI and listener maintenance share the same snapshot.
  const std::vector<Interface>& interfaces() const { return interfaces_; }
  void SetListenAll(bool enabled);
  unsigned PortFor(const std::string& address) const {
    return identity_.PortFor(identity_.listen_all() ? kAllInterfaces : address);
  }
  void Configure(const std::string& address, unsigned port);
  bool password_pending() const { return reset_job_.valid(); }
  void ResetPassword();
  void SetPassword(std::string password);
  bool listening() const { return !listeners_.empty(); }
  struct ListenerIssue {
    std::string address;
    unsigned port;
    std::string error;
  };
  std::string ListenerError(const std::string& address) const;
  std::optional<ListenerIssue> TakeListenerWarning();
  std::string TakeNewPassword();
  int RetryAfter(const std::string& address, unsigned port) const;
  SessionPtr Connect(const std::string& address, unsigned port,
                     std::string password);
  void Close(const SessionPtr& session, const std::string& error = {});
  void Tick();
  void Stop();
  bool Quiescent() const;
  std::string PendingWork() const;

 private:
  void Prepare(const SessionPtr& session);
  void Message(const SessionPtr& session, const std::string& text);
  bool Send(const SessionPtr& session, nlohmann::json message,
            bool sequenced = false);
  void Dispose(const SessionPtr& session);
  void Fail(const SessionPtr& session, const std::string& error,
            int retry_after = 0);
  void RefreshListeners();
  void ReplaceListener(const std::string& address, unsigned port, bool save,
                       bool allow_fallback = false);
  void RetireListener(uint64_t id);
  void SetListenerError(const std::string& address, unsigned port,
                        const std::string& error);
  Identity identity_;
  Hooks hooks_;
  Transport transport_;
  std::vector<Interface> interfaces_;
  std::map<uint64_t, SessionPtr> sessions_;
  struct AuthJob {
    SessionPtr session;
    std::future<DeviceAuthLimiter::Result> result;
  };
  struct PrepareJob {
    SessionPtr session;
    std::future<Prepared> result;
  };
  std::vector<AuthJob> auth_jobs_;
  std::vector<PrepareJob> prepare_jobs_;
  std::vector<std::future<void>> cleanup_jobs_;
  DeviceAuthLimiter auth_limiter_;
  CredentialWorkLimiter credential_work_limiter_;
  std::map<std::string, std::chrono::steady_clock::time_point> retry_at_;
  struct Listener {
    uint64_t id;
    unsigned port;
  };
  std::map<std::string, Listener> listeners_;
  std::map<std::string, ListenerIssue> listener_errors_, listener_warnings_;
  uint64_t epoch_ = 1;
  bool stopping_ = false, network_drained_ = false;
  std::chrono::steady_clock::time_point interface_check_{};
  std::future<std::pair<std::string, nlohmann::json>> reset_job_;
  std::string new_password_, access_password_;
};
}  // namespace crossdesk::lan

#endif
