#include "lan_session_manager.h"

#include <openssl/crypto.h>

#include <algorithm>
#include <thread>

namespace crossdesk::lan {
namespace {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
template <class T>
bool Ready(std::future<T>& future) {
  return future.valid() &&
         future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
void Check(bool condition) {
  if (!condition) throw std::runtime_error("Invalid LAN protocol message");
}
bool Id(const Json& value) {
  if (!value.is_string()) return false;
  const auto& s = value.get_ref<const std::string&>();
  return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
Json Capabilities() {
  return Json::array({"host-only-ipv4", "host-only-ipv6", "dtls-srtp-all",
                      "password-auth-v1"});
}
void Capable(const Json& j, bool ipv6) {
  const auto& caps = j.at("capabilities");
  Check(caps.is_array() && caps.size() <= 16);
  for (const auto* required : {ipv6 ? "host-only-ipv6" : "host-only-ipv4",
                               "dtls-srtp-all", "password-auth-v1"})
    Check(std::find(caps.begin(), caps.end(), required) != caps.end());
}
void Wipe(std::string& text) {
  if (!text.empty()) OPENSSL_cleanse(text.data(), text.size());
  text.clear();
}
const char* FailureReason(const Session& s, const char* fallback) {
  switch (s.failure_code.load()) {
    case MiniRtcDirectInvalidSignal:
      return "InvalidSignal";
    case MiniRtcDirectSignalSendFailed:
      return "SignalSendFailed";
    case MiniRtcDirectDtlsHandshakeFailed:
      return "DtlsHandshakeFailed";
    case MiniRtcDirectRequiredCryptoUnsupported:
      return "RequiredCryptoUnsupported";
    case MiniRtcDirectSrtpInstallFailed:
      return "SrtpInstallFailed";
    case MiniRtcDirectNetworkPolicyFailed:
      return "NetworkPolicyFailed";
    case MiniRtcDirectFingerprintMissing:
      return "FingerprintMissing";
    case MiniRtcDirectFingerprintMismatch:
      return "FingerprintMismatch";
    case MiniRtcDirectSrtpKeyExportFailed:
      return "SrtpKeyExportFailed";
    default:
      return fallback;
  }
}
}  // namespace
int Session::SendSignal(const char* data, size_t size, void* context) {
  auto* s = static_cast<Session*>(context);
  if (!s || s->closing || !data || !size || size > 256 * 1024) return -1;
  std::lock_guard lock(s->signal_mutex);
  if (s->closing || s->signal_count >= 256 ||
      s->signal_bytes + size > 1024 * 1024)
    return -1;
  ++s->signal_count;
  s->signal_bytes += size;
  s->signals.emplace_back(data, size);
  return 0;
}
void Session::Error(MiniRtcDirectError error, void* context) {
  if (auto* s = static_cast<Session*>(context)) {
    int empty = -1;
    s->failure_code.compare_exchange_strong(empty, error);
    s->failed = true;
  }
}
void Session::Status(MiniRtcConnectionStatus status) {
  if (closing) return;
  if (status == MiniRtcConnectionStatus::MiniRtcConnectionConnected)
    ready = true;
  else if (status == MiniRtcConnectionStatus::MiniRtcConnectionClosed ||
           status == MiniRtcConnectionStatus::MiniRtcConnectionFailed ||
           status == MiniRtcConnectionStatus::MiniRtcConnectionDisconnected)
    failed = true;
}
SessionManager::SessionManager(std::filesystem::path path, Hooks hooks)
    : identity_(std::move(path)), hooks_(std::move(hooks)) {
  identity_.Load();
  access_password_ = identity_.ReadPassword();
  new_password_ = access_password_;
  if (!Identity::CurrentVerifier(identity_.verifier())) {
    if (!access_password_.empty()) {
      // Migrate the saved password, retaining its value, identity and settings.
      SetPassword(access_password_);
    } else if (identity_.verifier().is_null()) {
      // Generate the initial password before automatically accepting
      // connections.
      ResetPassword();
    } else {
      hooks_.error("LAN password is not ready");
    }
  }
}
SessionManager::~SessionManager() {
  Stop();
  while (!Quiescent()) {
    Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  transport_.Shutdown();
  Wipe(new_password_);
  Wipe(access_password_);
}
void SessionManager::Configure(const std::string& address, unsigned port) {
  if (stopping_) return;
  if (!port || port > 65535) throw std::runtime_error("Invalid LAN port");
  if (!Identity::CurrentVerifier(identity_.verifier()))
    throw std::runtime_error("LAN password is not ready");
  ReplaceListener(identity_.listen_all() ? kAllInterfaces : address, port,
                  true);
}
void SessionManager::SetListenAll(bool enabled) {
  if (stopping_ || enabled == identity_.listen_all()) return;
  if (!Identity::CurrentVerifier(identity_.verifier()))
    throw std::runtime_error("LAN password is not ready");
  const auto interfaces = LocalIpInterfaces(!enabled);
  const auto previous = listeners_;
  // Wildcard and per-address listeners can overlap on the same port. Retire
  // acceptors before rebinding; authenticated channels retain their sockets.
  for (const auto& [_, listener] : previous) RetireListener(listener.id);
  listeners_.clear();
  std::map<std::string, unsigned> ports;
  try {
    const auto add = [&](const std::string& address) {
      ReplaceListener(address, identity_.PortFor(address), false,
                      !identity_.HasPortFor(address));
      ports[address] = listeners_.at(address).port;
    };
    if (enabled)
      add(kAllInterfaces);
    else
      for (const auto& interface : interfaces) add(interface.address);
    // Publish the mode and all newly allocated ports in one durable write.
    identity_.SaveListenerMode(enabled, ports);
  } catch (...) {
    for (const auto& [_, listener] : listeners_) RetireListener(listener.id);
    listeners_.clear();
    // Keep the original configuration and restore its exact ports. A failed
    // restore remains visible and is retried by ordinary listener maintenance.
    for (const auto& [address, listener] : previous) {
      try {
        ReplaceListener(address, listener.port, false);
      } catch (const std::exception& error) {
        SetListenerError(address, listener.port, error.what());
      }
    }
    throw;
  }
  interfaces_ = interfaces;
  listener_errors_.clear();
  listener_warnings_.clear();
}
void SessionManager::ReplaceListener(const std::string& address, unsigned port,
                                     bool save, bool allow_fallback) {
  const auto previous = listeners_.find(address);
  if (previous != listeners_.end() && previous->second.port == port) return;
  const auto next = [&] {
    try {
      return transport_.Listen(identity_, address, port);
    } catch (const std::exception& e) {
      const std::string error = e.what();
      if (!allow_fallback ||
          (error != "LanPortInUse" && error != "LanPortPermissionDenied" &&
           error != "LanPortCheckFailed"))
        throw;
      // Keep the OS-assigned socket bound through persistence/publication;
      // probing then releasing a free port would introduce an allocation race.
      return transport_.Listen(identity_, address, 0);
    }
  }();
  try {
    if (save) identity_.SaveSettings(address, next.port);
  } catch (...) {
    transport_.StopAccept(next.id);
    throw;
  }
  const auto old = previous == listeners_.end() ? 0 : previous->second.id;
  listeners_[address] = {next.id, next.port};
  SetListenerError(address, next.port, "");
  if (old) RetireListener(old);
}
void SessionManager::RetireListener(uint64_t id) {
  transport_.StopAccept(id);
  // Only cancel unfinished authentication on this retired endpoint. Other
  // addresses and sessions that already authenticated keep their channels.
  for (auto& [_, s] : sessions_)
    if (s->incoming && s->channel->listener == id &&
        s->phase < Session::AwaitOffer)
      Close(s, "ListenerChanged");
}
void SessionManager::ResetPassword() {
  if (stopping_ || reset_job_.valid()) return;
  reset_job_ =
      std::async(std::launch::async, [current = access_password_]() mutable {
        try {
          for (;;) {
            auto result = Identity::GeneratePassword();
            if (result.first != current) {
              Wipe(current);
              return result;
            }
            Wipe(result.first);
          }
        } catch (...) {
          Wipe(current);
          throw;
        }
      });
}
void SessionManager::SetPassword(std::string password) {
  if (!ValidPassword(password)) {
    Wipe(password);
    throw std::runtime_error(
        "LAN password must contain exactly 6 ASCII letters or digits");
  }
  if (stopping_ || reset_job_.valid() ||
      (password == access_password_ &&
       Identity::CurrentVerifier(identity_.verifier()))) {
    Wipe(password);
    return;
  }
  reset_job_ = std::async(
      std::launch::async, [password = std::move(password)]() mutable {
        try {
          auto record = Identity::RegisterPassword(password);
          return std::make_pair(std::move(password), std::move(record));
        } catch (...) {
          Wipe(password);
          throw;
        }
      });
}
void SessionManager::SetListenerError(const std::string& address, unsigned port,
                                      const std::string& error) {
  if (error.empty()) {
    listener_errors_.erase(address);
    listener_warnings_.erase(address);
    return;
  }
  const auto previous = listener_errors_.find(address);
  if (previous != listener_errors_.end() && previous->second.port == port &&
      previous->second.error == error)
    return;
  ListenerIssue issue{address, port, error};
  listener_errors_[address] = issue;
  listener_warnings_[address] = std::move(issue);
}
std::string SessionManager::ListenerError(const std::string& address) const {
  const auto it =
      listener_errors_.find(identity_.listen_all() ? kAllInterfaces : address);
  return it == listener_errors_.end() ? "" : it->second.error;
}
std::optional<SessionManager::ListenerIssue> SessionManager::TakeListenerWarning() {
  if (listener_warnings_.empty()) return std::nullopt;
  auto it = listener_warnings_.begin();
  auto issue = std::move(it->second);
  listener_warnings_.erase(it);
  return issue;
}
void SessionManager::RefreshListeners() {
  if (stopping_ || !Identity::CurrentVerifier(identity_.verifier())) return;
  const auto available = [&](const auto& address) {
    if (identity_.listen_all()) return address == kAllInterfaces;
    return std::any_of(interfaces_.begin(), interfaces_.end(),
                       [&](const auto& i) { return i.address == address; });
  };
  for (auto it = listeners_.begin(); it != listeners_.end();) {
    if (available(it->first)) {
      ++it;
      continue;
    }
    RetireListener(it->second.id);
    it = listeners_.erase(it);
  }
  for (auto it = listener_errors_.begin(); it != listener_errors_.end();) {
    if (available(it->first)) {
      ++it;
      continue;
    }
    listener_warnings_.erase(it->first);
    it = listener_errors_.erase(it);
  }
  const auto desired = identity_.listen_all()
                           ? std::vector<Interface>{{kAllInterfaces}}
                           : interfaces_;
  for (const auto& interface : desired) {
    const auto port = identity_.PortFor(interface.address);
    const bool initialize = !identity_.HasPortFor(interface.address);
    try {
      // Persist the first choice even when the default works. Later conflicts
      // must not silently change an endpoint users may already have shared.
      ReplaceListener(interface.address, port, initialize, initialize);
    } catch (const std::exception& e) {
      SetListenerError(interface.address, port, e.what());
    }
  }
}
std::string SessionManager::TakeNewPassword() {
  return std::exchange(new_password_, {});
}
SessionManager::SessionPtr SessionManager::Connect(const std::string& address,
                                                   unsigned port,
                                                   std::string password) {
  if (stopping_ || sessions_.size() >= 24)
    throw std::runtime_error("LAN connection limit reached");
  if (!ValidPassword(password))
    throw std::runtime_error(
        "LAN password must contain exactly 6 ASCII letters or digits");
  if (RetryAfter(address, port) > 0) {
    Wipe(password);
    throw std::runtime_error("AuthenticationRateLimited");
  }
  auto s = std::make_shared<Session>();
  const auto [ip, endpoint_port] = ParseEndpoint(address, port);
  s->target = Target{ip, {}, endpoint_port};
  s->channel = transport_.Connect(s->target);
  s->password = std::move(password);
  s->epoch = epoch_;
  s->key = "lan/" + RandomHex(16);
  s->deadline = Clock::now() + std::chrono::seconds(8);
  sessions_[s->channel->id] = s;
  return s;
}
bool SessionManager::Send(const SessionPtr& s, Json j, bool sequenced) {
  j["version"] = 2;
  if (sequenced) {
    j["session_id"] = s->session_id;
    j["seq"] = s->send_seq + 1;
  }
  if (s->closing || !transport_.Send(s->channel, j.dump())) {
    Close(s, "SignalSendFailed");
    return false;
  }
  if (sequenced) ++s->send_seq;
  return true;
}
int SessionManager::RetryAfter(const std::string& address,
                               unsigned port) const {
  const auto [ip, endpoint_port] = ParseEndpoint(address, port);
  const auto it = retry_at_.find(FormatEndpoint(ip, endpoint_port));
  const auto now = Clock::now();
  return it == retry_at_.end() || it->second <= now
             ? 0
             : static_cast<int>(
                   std::chrono::ceil<std::chrono::seconds>(it->second - now)
                       .count());
}
void SessionManager::Fail(const SessionPtr& s, const std::string& error,
                          int retry_after) {
  Json reply = {{"type", "error"}, {"error", error}};
  // Use the same failure codes and retry_after semantics as cloud Join.
  if (error == "InvalidCredentials")
    reply["error_code"] = "AUTHENTICATION_FAILED";
  if (error == "AuthenticationRateLimited")
    reply["error_code"] = "AUTHENTICATION_THROTTLED";
  if (error == "Busy") reply["error_code"] = "CREDENTIAL_SERVICE_BUSY";
  if (retry_after > 0) reply["retry_after"] = retry_after;
  if (s->session_id.empty() ||
      (s->incoming && s->phase < Session::AwaitOffer)) {
    reply["request_id"] = s->auth_request;
    Send(s, std::move(reply));
  } else {
    Send(s, std::move(reply), true);
  }
  Close(s, error);
}
void SessionManager::Prepare(const SessionPtr& s) {
  size_t admitted = 0;
  for (auto& [_, other] : sessions_)
    if (!other->closing && (other->phase == Session::Preparing ||
                            other->phase >= Session::AwaitOffer))
      ++admitted;
  if (admitted >= 8 || prepare_jobs_.size() >= 2) {
    Fail(s, "Busy");
    return;
  }
  s->phase = Session::Preparing;
  if (s->incoming) {
    s->session_id = RandomHex(16);
    s->local_id = identity_.id();
    s->remote_id = RandomHex(16);
  }
  auto work = hooks_.prepare(s);
  prepare_jobs_.push_back(
      {s, std::async(std::launch::async, [s, work = std::move(work)] {
         if (s->closing) return Prepared{};
         Prepared prepared = work();
         if (!prepared.peer || s->closing) return prepared;
         MiniRtcDirectOptions options{};
         options.struct_size = sizeof(options);
         options.version = 1;
         options.local_id = s->local_id.c_str();
         options.remote_id = s->remote_id.c_str();
         options.session_id = s->session_id.c_str();
         options.local_address = s->channel->local_address.c_str();
         options.remote_address = s->channel->remote_address.c_str();
         options.offerer = !s->incoming;
         options.send_signal = Session::SendSignal;
         options.on_error = Session::Error;
         options.context = s.get();
         if (MiniRtcInitDirectPeer(prepared.peer, &options) != 0 ||
             (!s->closing &&
              MiniRtcStartDirectSession(prepared.peer, !s->incoming) != 0))
           s->failed = true;
         return prepared;
       })});
}
void SessionManager::Message(const SessionPtr& s, const std::string& text) {
  auto j = Json::parse(text);
  Check(j.is_object() && j.at("version").is_number_integer() &&
        j.at("version") == 2 && j.at("type").is_string() &&
        !j.contains("user_id") && !j.contains("remote_user_id"));
  const auto type = j.at("type").get<std::string>();
  if (type == "error" && (s->session_id.empty() ||
                          (s->incoming && s->phase < Session::AwaitOffer))) {
    Check(!j.contains("session_id") && !j.contains("seq") &&
          j.at("request_id") == s->auth_request);
    const auto error = j.at("error").get<std::string>();
    Check(error.size() <= 80);
    if (!s->incoming &&
        (error == "AuthenticationRateLimited" || error == "Busy") &&
        j.contains("retry_after")) {
      Check(j["retry_after"].is_number_integer() && j["retry_after"] > 0 &&
            j["retry_after"] <= 900);
      const auto now = Clock::now();
      for (auto it = retry_at_.begin(); it != retry_at_.end();)
        if (it->second <= now)
          it = retry_at_.erase(it);
        else
          ++it;
      if (retry_at_.size() < 64)
        retry_at_[FormatEndpoint(s->target.address, s->target.port)] =
            now + std::chrono::seconds(j["retry_after"].get<int>());
    }
    Close(s, error);
    return;
  }
  if (s->phase == Session::Preparing && !s->incoming) {
    Check(j.at("session_id") == s->session_id &&
          j.at("seq").is_number_unsigned() &&
          j.at("seq").get<uint64_t>() ==
              s->receive_seq + s->pending_receive.size() + 1 &&
          !j.contains("request_id"));
    // Identity is already bound by auth_result. Process cancellation without
    // waiting for Peer preparation, before the following WSS close can erase
    // its reason; earlier queued SDP/candidates never grant authorization.
    if (type == "bye" || type == "error") {
      const auto reason = type == "bye" ? std::string("RemoteClosed")
                                        : j.at("error").get<std::string>();
      Check(!reason.empty() && reason.size() <= 80);
      Close(s, reason);
      return;
    }
    Check(type == "answer" || type == "candidate");
    Check(s->pending_receive.size() < 128);
    s->pending_receive.push_back(text);
    return;
  }
  if (s->phase == Session::AwaitAuth) {
    Check(type == "auth" && !j.contains("session_id") && !j.contains("seq") &&
          Id(j.at("request_id")) && j.at("password").is_string() &&
          !j.contains("share") && !j.contains("proof"));
    Capable(j, s->channel->remote_address.find(':') != std::string::npos);
    s->auth_request = j.at("request_id");
    auto& submitted = j["password"].get_ref<std::string&>();
    if (!ValidPassword(submitted)) {
      Wipe(submitted);
      Fail(s, "InvalidCredentials");
      return;
    }
    if (auth_jobs_.size() >= 2) {
      Wipe(submitted);
      Fail(s, "Busy", 1);
      return;
    }
    const auto delay =
        credential_work_limiter_.Admit(s->channel->remote_address);
    if (delay > 0) {
      Wipe(submitted);
      Fail(s, "AuthenticationRateLimited", delay);
      return;
    }
    s->phase = Session::Authenticating;
    auth_jobs_.push_back(
        {s,
         std::async(std::launch::async,
                    [this, password = std::exchange(submitted, {}),
                     record = identity_.verifier(),
                     source = s->channel->remote_address,
                     target = identity_.id()]() mutable {
                      try {
                        auto result = auth_limiter_.Verify(source, target, [&] {
                          return Identity::VerifyPassword(record, password);
                        });
                        Wipe(password);
                        return result;
                      } catch (...) {
                        Wipe(password);
                        throw;
                      }
                    })});
    return;
  }
  if (s->phase == Session::AwaitAuthResult) {
    Check(type == "auth_result" && j.at("request_id") == s->auth_request &&
          !j.contains("seq"));
    if (!j.value("success", false)) {
      Close(s, "InvalidCredentials");
      return;
    }
    Check(Id(j.at("session_id")) && Id(j.at("host_peer_id")) &&
          Id(j.at("controller_peer_id")) &&
          j.at("controller_peer_id") != j.at("host_peer_id"));
    s->target.device_id = j.at("host_peer_id");
    s->session_id = j.at("session_id");
    s->local_id = j.at("controller_peer_id");
    s->remote_id = j.at("host_peer_id");
    Prepare(s);
    return;
  }
  Check(s->peer && j.at("session_id") == s->session_id &&
        j.at("seq").is_number_unsigned() &&
        j.at("seq").get<uint64_t>() == s->receive_seq + 1 &&
        !j.contains("request_id"));
  ++s->receive_seq;
  if (type == "bye" || type == "error") {
    const auto reason = type == "bye" ? std::string("RemoteClosed")
                                      : j.at("error").get<std::string>();
    Check(!reason.empty() && reason.size() <= 80);
    Close(s, reason);
    return;
  }
  if (type == "offer") {
    Check(s->incoming && s->phase == Session::AwaitOffer);
    s->phase = Session::Negotiating;
    s->deadline = Clock::now() + std::chrono::seconds(15);
  } else if (type == "answer") {
    Check(!s->incoming && s->phase == Session::AwaitAnswer);
    s->phase = Session::Negotiating;
  } else
    Check(type == "candidate" &&
          (s->phase == Session::AwaitAnswer ||
           s->phase == Session::Negotiating || s->phase == Session::Connected));
  if (MiniRtcReceiveDirectSignal(s->peer, text.data(), text.size()) != 0)
    Fail(s, "InvalidSignal");
}
void SessionManager::Dispose(const SessionPtr& s) {
  if (!s->peer) return;
  auto* peer = std::exchange(s->peer, nullptr);
  auto owner = s->callback_owner;
  auto drain = s->drain_callbacks;
  cleanup_jobs_.push_back(
      std::async(std::launch::async, [s, peer, owner, drain]() mutable {
        if (drain) drain();
        MiniRtcCloseDirectSession(peer);
        MiniRtcDestroyPeer(&peer);
      }));
}
void SessionManager::Close(const SessionPtr& s, const std::string& error) {
  if (!s || s->closing.exchange(true)) return;
  s->authorized = false;
  if (error.empty() && !s->session_id.empty() &&
      s->phase >= Session::AwaitOffer)
    transport_.Send(s->channel, Json{{"version", 2},
                                     {"type", "bye"},
                                     {"session_id", s->session_id},
                                     {"seq", ++s->send_seq}}
                                    .dump());
  s->phase = Session::Closing;
  s->error = error;
  Wipe(s->password);
  transport_.Close(s->channel);
  hooks_.closed(s);
  Dispose(s);
  if (!error.empty() && !s->incoming && !stopping_) hooks_.error(error);
}
void SessionManager::Stop() {
  if (stopping_) return;
  stopping_ = true;
  for (const auto& [_, listener] : listeners_) transport_.StopAccept(listener.id);
  listeners_.clear();
  listener_errors_.clear();
  listener_warnings_.clear();
  for (auto& [_, s] : sessions_) Close(s);
}
bool SessionManager::Quiescent() const {
  return (!stopping_ || network_drained_) && sessions_.empty() &&
         auth_jobs_.empty() && prepare_jobs_.empty() && cleanup_jobs_.empty() &&
         !reset_job_.valid();
}
std::string SessionManager::PendingWork() const {
  return "sessions=" + std::to_string(sessions_.size()) +
         ", auth=" + std::to_string(auth_jobs_.size()) +
         ", preparing=" + std::to_string(prepare_jobs_.size()) +
         ", cleanup=" + std::to_string(cleanup_jobs_.size()) +
         ", reset=" + std::to_string(reset_job_.valid()) +
         ", network_drained=" + std::to_string(network_drained_);
}
void SessionManager::Tick() {
  if (Ready(reset_job_)) {
    try {
      auto [password, verifier] = reset_job_.get();
      if (!stopping_) {
        const bool changed = verifier != identity_.verifier();
        try {
          identity_.SetPassword(password, verifier);
        } catch (...) {
          Wipe(password);
          throw;
        }
        if (changed) ++epoch_;
        Wipe(new_password_);
        Wipe(access_password_);
        access_password_ = password;
        new_password_ = std::move(password);
        for (auto& [_, s] : sessions_)
          if (s->incoming && !s->authorized && s->epoch != epoch_)
            Fail(s, "CredentialsChanged");
        interface_check_ = {};  // Start listening as soon as credentials are ready.
      } else
        Wipe(password);
    } catch (const std::exception& e) {
      hooks_.error(e.what());
    }
  }
  for (auto it = auth_jobs_.begin(); it != auth_jobs_.end();) {
    if (!Ready(it->result)) {
      ++it;
      continue;
    }
    auto s = it->session;
    DeviceAuthLimiter::Result result;
    bool failed = false;
    try {
      result = it->result.get();
    } catch (...) {
      failed = true;
    }
    it = auth_jobs_.erase(it);
    if (s->closing || stopping_ || s->epoch != epoch_) continue;
    if (failed) {
      Fail(s, "Busy", 1);
    } else if (result.retry_after > 0) {
      Fail(s, "AuthenticationRateLimited", result.retry_after);
    } else if (!result.authenticated) {
      Fail(s, "InvalidCredentials");
    } else {
      try {
        Prepare(s);
      } catch (...) {
        Fail(s, "PeerPreparationFailed");
      }
    }
  }
  for (auto it = prepare_jobs_.begin(); it != prepare_jobs_.end();) {
    if (!Ready(it->result)) {
      ++it;
      continue;
    }
    auto s = it->session;
    try {
      auto p = it->result.get();
      s->peer = p.peer;
      s->callback_owner = std::move(p.callback_owner);
      s->drain_callbacks = std::move(p.drain_callbacks);
    } catch (...) {
      s->failed = true;
    }
    it = prepare_jobs_.erase(it);
    if (s->closing || stopping_ || (s->incoming && s->epoch != epoch_)) {
      Dispose(s);
      continue;
    }
    if (!s->peer || s->failed) {
      Fail(s, FailureReason(*s, "PeerPreparationFailed"));
      continue;
    }
    hooks_.attached(s);
    transport_.Authenticated(s->channel);
    if (s->incoming) {
      s->phase = Session::AwaitOffer;
      s->deadline = Clock::now() + std::chrono::seconds(5);
      Send(s, {{"type", "auth_result"},
               {"request_id", s->auth_request},
               {"success", true},
               {"session_id", s->session_id},
               {"host_peer_id", s->local_id},
               {"controller_peer_id", s->remote_id}});
    } else {
      s->phase = Session::AwaitAnswer;
      s->deadline = Clock::now() + std::chrono::seconds(15);
      while (!s->pending_receive.empty() && !s->closing) {
        auto text = std::move(s->pending_receive.front());
        s->pending_receive.pop_front();
        try {
          Message(s, text);
        } catch (...) {
          Fail(s, "InvalidSignal");
        }
      }
    }
  }
  for (auto& event : transport_.Poll()) {
    auto it = sessions_.find(event.channel->id);
    if (it == sessions_.end()) {
      const auto listener = std::find_if(
          listeners_.begin(), listeners_.end(), [&](const auto& entry) {
            return entry.second.id == event.channel->listener;
          });
      if (event.kind != Transport::Event::Open || !event.channel->incoming ||
          stopping_ || listener == listeners_.end() ||
          event.channel->listener != listener->second.id) {
        transport_.Close(event.channel);
        continue;
      }
      auto s = std::make_shared<Session>();
      s->channel = event.channel;
      s->incoming = true;
      s->key = "lan/" + RandomHex(16);
      s->epoch = epoch_;
      sessions_[event.channel->id] = s;
      it = sessions_.find(event.channel->id);
    }
    auto s = it->second;
    if (s->closing) continue;
    if (event.kind == Transport::Event::Open) {
      s->deadline = Clock::now() + std::chrono::seconds(10);
      s->phase = s->incoming ? Session::AwaitAuth : Session::AwaitAuthResult;
      if (!s->incoming) {
        s->auth_request = RandomHex(16);
        Json auth = {{"type", "auth"},
                     {"request_id", s->auth_request},
                     {"password", std::exchange(s->password, {})},
                     {"capabilities", Capabilities()}};
        Send(s, auth);
        Wipe(auth["password"].get_ref<std::string&>());
      }
    } else if (event.kind == Transport::Event::Closed)
      Close(s, event.payload.empty() ? "ConnectionClosed" : event.payload);
    else
      try {
        Message(s, event.payload);
      } catch (...) {
        Fail(s, "InvalidSignal");
      }
    Wipe(event.payload);
  }
  const auto now = Clock::now();
  for (auto& [_, s] : sessions_) {
    if (s->closing) continue;
    if (s->failed) {
      Fail(s, FailureReason(*s, "SecureTransportFailed"));
      continue;
    }
    if (s->phase != Session::Connected && now >= s->deadline) {
      Fail(s, s->phase == Session::Negotiating ? "IceConnectionTimeout"
                                               : "Timeout");
      continue;
    }
    std::deque<std::string> signals;
    {
      std::lock_guard lock(s->signal_mutex);
      signals.swap(s->signals);
    }
    for (const auto& raw : signals) try {
        auto j = Json::parse(raw);
        Check(j.at("transmission_id") == s->session_id &&
              j.at("user_id") == s->local_id &&
              j.at("remote_user_id") == s->remote_id);
        const auto type = j.at("type").get<std::string>();
        Json out;
        if (type == "offer" || type == "answer")
          out = {{"type", type}, {"sdp", j.at("sdp")}};
        else {
          Check(type == "new_candidate");
          out = {{"type", "candidate"}, {"ufrag", j.at("ufrag")}};
          if (j.at("sdp") == "a=end-of-candidates\r\n")
            out["end_of_candidates"] = true;
          else
            out["candidate"] = j.at("sdp");
        }
        if (!Send(s, std::move(out), true)) break;
      } catch (...) {
        Fail(s, "InvalidLocalSignal");
        break;
      }
    if (s->ready.exchange(false) && !s->closing && !s->failed) {
      if (s->phase != Session::Negotiating || stopping_ ||
          (s->incoming && s->epoch != epoch_)) {
        Fail(s, "CredentialsChanged");
        continue;
      }
      s->phase = Session::Connected;
      s->authorized = true;
      hooks_.connected(s);
    }
  }
  if (!stopping_ && now - interface_check_ >= std::chrono::seconds(2)) {
    interface_check_ = now;
    const auto local_interfaces = LocalIpInterfaces();
    interfaces_ =
        identity_.listen_all() ? local_interfaces : LocalIpInterfaces(true);
    RefreshListeners();
    for (auto& [_, s] : sessions_)
      if (s->phase != Session::Connecting &&
          !s->channel->local_address.empty() &&
          std::none_of(local_interfaces.begin(), local_interfaces.end(),
                       [&](const auto& i) {
                         return i.address == s->channel->local_address;
                       }))
        Close(s, "InterfaceRemoved");
  }
  for (auto it = cleanup_jobs_.begin(); it != cleanup_jobs_.end();)
    if (Ready(*it)) {
      try {
        it->get();
      } catch (...) {
      }
      it = cleanup_jobs_.erase(it);
    } else
      ++it;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    const auto& s = it->second;
    const bool pending =
        std::any_of(auth_jobs_.begin(), auth_jobs_.end(),
                    [&](const auto& j) { return j.session == s; }) ||
        std::any_of(prepare_jobs_.begin(), prepare_jobs_.end(),
                    [&](const auto& j) { return j.session == s; });
    if (s->closing && !pending) {
      s->phase = Session::Closed;
      it = sessions_.erase(it);
    } else
      ++it;
  }
  if (stopping_ && !network_drained_ && sessions_.empty() &&
      auth_jobs_.empty() && prepare_jobs_.empty() && cleanup_jobs_.empty() &&
      !reset_job_.valid()) {
    transport_.Shutdown();
    network_drained_ = true;
  }
}
}  // namespace crossdesk::lan
