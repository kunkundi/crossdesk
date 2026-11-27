#include "mobile_direct_connection.h"

#include <cstdio>
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
  if (input.empty()) return {};
  try {
    const auto [ip, port] = lan::ParseEndpoint(input, kDefaultPort);
    return lan::FormatEndpoint(ip, port);
  } catch (const std::exception&) {
    return {};
  }
}

bool DirectConnection::IsDomain(const std::string& input) {
  try {
    return lan::ParseDialEndpoint(input, kDefaultPort).kind ==
           lan::DialEndpoint::DnsName;
  } catch (const std::exception&) {
    return false;
  }
}

bool DirectConnection::Start(MiniRtcPeerPtr* peer, const std::string& endpoint,
                             const std::string& password,
                             const std::string& expected_key, bool saved,
                             Notify notify) {
  if (!peer || impl_->worker.joinable() || !lan::ValidPassword(password)) return false;
  try {
    impl_->stopping = false;
    lan::SessionManager::Hooks hooks;
    hooks.attached = [](const auto&) {};
    hooks.connected = [notify](const auto& session) {
      notify(MiniRtcConnectionConnected, session->channel->peer_fingerprint);
    };
    hooks.closed = [this, notify](const auto& session) {
      if (impl_->stopping) return;
      const auto& reason = session->error;
      auto status = MiniRtcConnectionFailed;
      if (reason == "InvalidCredentials") status = MiniRtcConnectionIncorrectPassword;
      if (reason == "AuthenticationRateLimited" || reason == "Busy")
        status = MiniRtcConnectionRemoteUnavailable;
      if (reason == "RemoteClosed") status = MiniRtcConnectionClosed;
      std::string detail = reason;
      if (reason == "LocalNameFailed" || reason == "LocalNameTimeout") {
        detail = reason == "LocalNameTimeout" ? "域名解析超时" : "无法解析域名";
        detail += "\n请检查同一局域网、本地名称解析和本地网络权限";
      } else if (reason == "DnsFailed")
        detail = "无法解析域名";
      else if (reason == "DnsTimeout")
        detail = "域名解析超时";
      else if (reason == "DnsNoUsableAddress")
        detail = "域名没有可用于直连的地址";
      else if (reason == "DirectPortUnreachable")
        detail =
            "无法连接目标端口\n" +
            lan::FormatEndpoint(session->target.address, session->target.port);
      else if (reason == "InvalidCertificate" || reason == "FingerprintMissing")
        detail = "直连证书无效";
      else if (reason == "IncompatibleProtocol")
        detail = "直连协议不兼容";
      else if (reason == "IceConnectionTimeout" ||
               reason == "UnsupportedNetworkPath")
        detail = "请检查直连 UDP 路径";
      else if (!session->observed_host_key.empty())
        detail = "HostKeyChanged:" + session->observed_host_key;
      else
        detail.clear();
      notify(status, detail);
    };
    hooks.error = [](const auto&) {};
    hooks.diagnostic = [](const std::string& line) {
      std::fprintf(stderr, "CrossDesk LAN dial %s\n", line.c_str());
    };
    auto manager = std::make_unique<lan::SessionManager>(peer, std::move(hooks));
    auto session =
        manager->Connect(endpoint, kDefaultPort, password, expected_key, saved);
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

std::string DirectConnection::RemotePeerId() const {
  auto session = std::atomic_load(&impl_->session);
  if (!session || !session->authorized || session->closing) return {};
  return session->remote_id;
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
