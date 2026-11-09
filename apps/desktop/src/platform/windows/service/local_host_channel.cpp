#include "local_host_channel.h"

#include <sddl.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cstring>
#include <string>
#include "rd_log.h"

namespace crossdesk {
namespace {
constexpr size_t kMaxPacket = 1024 * 1024;
constexpr size_t kMaxQueue = 8 * kMaxPacket;
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::wstring ImagePath(HANDLE process) {
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) return {};
  path.resize(size);
  return path;
}
std::vector<uint8_t> User(HANDLE token) {
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<uint8_t> result(size);
  if (!size || !GetTokenInformation(token, TokenUser, result.data(), size, &size))
    result.clear();
  return result;
}
}  // namespace

LocalHostChannel::LocalHostChannel(bool server) : server_(server) {
  ProcessIdToSessionId(GetCurrentProcessId(), &session_);
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_ && session_) worker_ = std::thread([this] { Run(); });
}
LocalHostChannel::~LocalHostChannel() {
  if (stop_) SetEvent(stop_);
  if (worker_.joinable()) worker_.join();
  if (stop_) CloseHandle(stop_);
}

bool LocalHostChannel::Authenticate(HANDLE pipe, std::string& reason) const {
  reason = "pipe peer PID unavailable";
  ULONG pid = 0;
  if (!(server_ ? GetNamedPipeClientProcessId(pipe, &pid)
                : GetNamedPipeServerProcessId(pipe, &pid))) return false;
  DWORD session = 0;
  reason = "pipe peer belongs to another session";
  if (!ProcessIdToSessionId(pid, &session) || session != session_) return false;
  Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
  const auto expected = ImagePath(GetCurrentProcess());
  reason = "pipe peer executable does not match this installation";
  if (!process.value || expected.empty() ||
      _wcsicmp(expected.c_str(), ImagePath(process.value).c_str())) return false;
  Handle token;
  reason = "pipe peer token cannot be queried";
  if (!OpenProcessToken(process.value, TOKEN_QUERY, &token.value)) return false;
  const auto user = User(token.value);
  if (user.empty()) return false;
  const auto sid = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
  if (!server_) {
    reason = "pipe server is not SYSTEM";
    return IsWellKnownSid(sid, WinLocalSystemSid);
  }
  // A different elevated user must not attach to this console user's files,
  // clipboard, or host controls. The pipe DACL also excludes non-admin callers.
  Handle console_token;
  reason = "console user token unavailable";
  if (!WTSQueryUserToken(session_, &console_token.value)) return false;
  const auto console_user = User(console_token.value);
  reason = "pipe client is not the console user";
  return !console_user.empty() && EqualSid(sid,
      reinterpret_cast<const TOKEN_USER*>(console_user.data())->User.Sid);
}

bool LocalHostChannel::Read(HANDLE pipe, HANDLE event, void* data, DWORD size) {
  if (!ResetEvent(event)) return false;
  OVERLAPPED operation{};
  operation.hEvent = event;
  DWORD completed = 0;
  const BOOL ok = ReadFile(pipe, data, size, &completed, &operation);
  if (!ok) {
    if (GetLastError() != ERROR_IO_PENDING) return false;
    HANDLE waits[] = {stop_, event};
    if (WaitForMultipleObjects(2, waits, FALSE, 1000) != WAIT_OBJECT_0 + 1) {
      CancelIoEx(pipe, &operation);
      GetOverlappedResult(pipe, &operation, &completed, TRUE);
      return false;
    }
    if (!GetOverlappedResult(pipe, &operation, &completed, FALSE)) return false;
  }
  return completed == size;
}

bool LocalHostChannel::Send(const nlohmann::json& message) {
  if (!connected_) return false;
  auto bytes = nlohmann::json::to_cbor(message);
  if (bytes.empty() || bytes.size() > kMaxPacket) return false;
  const uint32_t size = static_cast<uint32_t>(bytes.size());
  bytes.insert(bytes.begin(), sizeof(size), 0);
  std::memcpy(bytes.data(), &size, sizeof(size));
  std::lock_guard lock(mutex_);
  if (!connected_ || outgoing_bytes_ + bytes.size() > kMaxQueue) return false;
  outgoing_bytes_ += bytes.size();
  outgoing_.push_back(std::move(bytes));
  return true;
}
void LocalHostChannel::Reconnect() {
  std::lock_guard lock(mutex_);
  connected_ = false;
}
std::vector<nlohmann::json> LocalHostChannel::Receive() {
  std::lock_guard lock(mutex_);
  std::vector<nlohmann::json> result;
  result.swap(incoming_);
  incoming_bytes_ = 0;
  return result;
}
bool LocalHostChannel::QueueIncoming(nlohmann::json message, size_t size) {
  std::lock_guard lock(mutex_);
  if (incoming_bytes_ + size > kMaxQueue) return false;
  incoming_bytes_ += size;
  incoming_.push_back(std::move(message));
  return true;
}
void LocalHostChannel::SetConnected(bool connected) {
  std::lock_guard lock(mutex_);
  connected_ = connected;
  outgoing_.clear();
  outgoing_bytes_ = 0;
  if (!connected) {
    incoming_.clear();
    incoming_bytes_ = 0;
  }
  incoming_.push_back({{"op", connected ? "attached" : "detached"}});
}

void LocalHostChannel::Run() {
  const auto name = L"\\\\.\\pipe\\CrossDesk.Host." + std::to_wstring(session_);
  std::string last_auth_error;
  while (WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT) {
    Handle pipe;
    if (server_) {
      PSECURITY_DESCRIPTOR descriptor = nullptr;
      if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
              L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1,
              &descriptor, nullptr)) return;
      SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
      pipe.value = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX |
          FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
          1, 65536, 65536, 0, &security);
      LocalFree(descriptor);
      if (pipe.value != INVALID_HANDLE_VALUE) {
        Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        OVERLAPPED connection{};
        connection.hEvent = event.value;
        const BOOL accepted = ConnectNamedPipe(pipe.value, &connection);
        const auto error = accepted ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_IO_PENDING) {
          HANDLE waits[] = {stop_, event.value};
          if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
            CancelIoEx(pipe.value, &connection);
            DWORD unused = 0;
            GetOverlappedResult(pipe.value, &connection, &unused, TRUE);
            return;
          }
        } else if (error != ERROR_SUCCESS && error != ERROR_PIPE_CONNECTED) {
          WaitForSingleObject(stop_, 250);
          continue;
        }
      }
    } else {
      pipe.value = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
          0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED |
          SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    }
    if (pipe.value == INVALID_HANDLE_VALUE) {
      WaitForSingleObject(stop_, 250);
      continue;
    }
    std::string auth_error;
    if (!Authenticate(pipe.value, auth_error)) {
      if (auth_error != last_auth_error)
        LOG_WARN("Local host channel authentication rejected: {}", auth_error);
      last_auth_error = auth_error;
      WaitForSingleObject(stop_, 250);
      continue;
    }
    last_auth_error.clear();
    Handle read_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle write_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!read_event.value || !write_event.value) return;
    SetConnected(true);
    std::vector<uint8_t> input;
    std::vector<uint8_t> output;
    OVERLAPPED writing{};
    writing.hEvent = write_event.value;
    bool write_pending = false;
    ULONGLONG write_started = 0;
    while (connected_ && WaitForSingleObject(stop_, 2) == WAIT_TIMEOUT) {
      if (write_pending) {
        DWORD completed = 0;
        if (GetOverlappedResult(pipe.value, &writing, &completed, FALSE)) {
          if (completed != output.size()) break;
          write_pending = false;
          output.clear();
        } else if (GetLastError() != ERROR_IO_INCOMPLETE ||
                   GetTickCount64() - write_started > 5000) break;
      }
      if (!write_pending) {
        std::lock_guard lock(mutex_);
        if (!outgoing_.empty()) {
          output = std::move(outgoing_.front());
          outgoing_.pop_front();
          outgoing_bytes_ -= output.size();
        }
      }
      if (!write_pending && !output.empty()) {
        ResetEvent(write_event.value);
        DWORD completed = 0;
        if (WriteFile(pipe.value, output.data(), static_cast<DWORD>(output.size()),
                      &completed, &writing)) {
          if (completed != output.size()) break;
          output.clear();
        } else {
          if (GetLastError() != ERROR_IO_PENDING) break;
          write_pending = true;
          write_started = GetTickCount64();
        }
      }
      // Drain the other direction even while a large write is waiting. Both
      // processes may be sending files at once; blocking writes would deadlock.
      DWORD available = 0;
      if (!PeekNamedPipe(pipe.value, nullptr, 0, nullptr, &available, nullptr)) break;
      if (!available) continue;
      const DWORD count = std::min<DWORD>(available, 65536);
      const auto offset = input.size();
      input.resize(offset + count);
      if (!Read(pipe.value, read_event.value, input.data() + offset, count)) break;
      bool valid = true;
      size_t consumed = 0;
      while (input.size() - consumed >= sizeof(uint32_t)) {
        uint32_t size = 0;
        std::memcpy(&size, input.data() + consumed, sizeof(size));
        if (!size || size > kMaxPacket) { valid = false; break; }
        if (input.size() - consumed < sizeof(size) + size) break;
        auto message = nlohmann::json::from_cbor(
            input.begin() + consumed + sizeof(size),
            input.begin() + consumed + sizeof(size) + size, true, false);
        if (!message.is_object() || !QueueIncoming(std::move(message), size)) {
          valid = false;
          break;
        }
        consumed += sizeof(size) + size;
      }
      if (!valid) break;
      input.erase(input.begin(), input.begin() + consumed);
    }
    if (write_pending) {
      CancelIoEx(pipe.value, &writing);
      DWORD completed = 0;
      GetOverlappedResult(pipe.value, &writing, &completed, TRUE);
    }
    SetConnected(false);
    if (server_) DisconnectNamedPipe(pipe.value);
  }
}
}  // namespace crossdesk
