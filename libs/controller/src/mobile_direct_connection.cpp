#include "mobile_direct_connection.h"

#include <thread>

#include "lan_session_manager.h"

namespace crossdesk::mobile_controller {
namespace {
constexpr unsigned kDefaultPort = 52888;
}
struct DirectConnection::Impl {
  MiniRtcPeerPtr* peer = nullptr;
  std::shared_ptr<lan::Session> session;
  std::atomic<bool> stopping{false};
  std::thread worker;
};

DirectConnection::DirectConnection() : impl_(std::make_unique<Impl>()) {}
DirectConnection::~DirectConnection() { Stop(); }

std::string DirectConnection::Endpoint(const std::string& input) {
  if (input.empty() || input.size() > 160) return {};
  try {
    const auto [ip, port] = lan::ParseEndpoint(input, kDefaultPort);
    return lan::FormatEndpoint(ip, port);
  } catch (const std::exception&) {
    return {};
  }
}

bool DirectConnection::Start(MiniRtcPeerPtr* peer, const std::string& endpoint,
                             const std::string& password,
                             std::function<void(MiniRtcConnectionStatus)> notify) {
  if (!peer || impl_->worker.joinable() || !lan::ValidPassword(password)) return false;
  try {
    impl_->stopping = false;
    lan::SessionManager::Hooks hooks;
    hooks.attached = [](const auto&) {};
    hooks.connected = [notify](const auto&) { notify(MiniRtcConnectionConnected); };
    hooks.closed = [this, notify](const auto& session) {
      if (impl_->stopping) return;
      const auto& reason = session->error;
      auto status = MiniRtcConnectionFailed;
      if (reason == "InvalidCredentials") status = MiniRtcConnectionIncorrectPassword;
      if (reason == "AuthenticationRateLimited" || reason == "Busy")
        status = MiniRtcConnectionRemoteUnavailable;
      if (reason == "RemoteClosed") status = MiniRtcConnectionClosed;
      notify(status);
    };
    hooks.error = [](const auto&) {};
    auto manager = std::make_unique<lan::SessionManager>(peer, std::move(hooks));
    auto session = manager->Connect(endpoint, kDefaultPort, password);
    impl_->peer = peer;
    std::atomic_store(&impl_->session, session);
    impl_->worker = std::thread([this, manager = std::move(manager), session] {
      while (!impl_->stopping && !session->closing) {
        manager->Tick();
        std::this_thread::sleep_for(session->authorized ? std::chrono::milliseconds(50)
                                                       : std::chrono::milliseconds(10));
      }
      // Manager destruction drains pending preparation before Stop closes the peer.
    });
    return true;
  } catch (const std::exception&) {
    Stop();
    return false;
  }
}

bool DirectConnection::Status(MiniRtcConnectionStatus status) {
  auto session = std::atomic_load(&impl_->session);
  if (!session) return false;
  session->Status(status);
  return true;
}

void DirectConnection::Stop() {
  impl_->stopping = true;
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->peer) {
    MiniRtcCloseDirectSession(impl_->peer);
    impl_->peer = nullptr;
  }
  std::atomic_store(&impl_->session, std::shared_ptr<lan::Session>{});
}
}  // namespace crossdesk::mobile_controller
