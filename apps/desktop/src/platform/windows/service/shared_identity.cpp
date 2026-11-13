#include "shared_identity.h"

#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <stdexcept>
#include <set>

#include "common/credential_cache.h"
#include "private_file.h"
#include "unattended_config.h"

namespace crossdesk {
namespace {
using Json = nlohmann::json;
constexpr wchar_t kSnapshot[] = L"shared-identity.json";
constexpr wchar_t kGuiOwner[] = L"gui-owner.json";

struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void Require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
uint64_t Created(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
  return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
std::wstring ProcessPath(HANDLE process) {
  wchar_t path[32768]{};
  DWORD count = 32768;
  if (!QueryFullProcessImageNameW(process, 0, path, &count)) return {};
  return {path, count};
}
std::string Read(const std::filesystem::path& path, DWORD limit = 256 * 1024) {
  const DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  Handle file{path.parent_path() == UnattendedDataPath()
      ? unattended_detail::OpenPrivateMachineFile(UnattendedDataPath(), path,
                                                 GENERIC_READ, sharing, OPEN_EXISTING)
      : CreateFileW(path.c_str(), GENERIC_READ, sharing, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
  Require(file.value != INVALID_HANDLE_VALUE, "Cannot open shared identity data");
  BY_HANDLE_FILE_INFORMATION info{};
  Require(GetFileInformationByHandle(file.value, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
      info.nNumberOfLinks == 1 && !info.nFileSizeHigh && info.nFileSizeLow <= limit,
      "Invalid shared identity file");
  std::string value(info.nFileSizeLow, '\0');
  DWORD read = 0;
  Require(ReadFile(file.value, value.data(), static_cast<DWORD>(value.size()), &read,
                   nullptr) && read == value.size(), "Cannot read shared identity data");
  return value;
}
Json Document(const std::filesystem::path& path) {
  auto document = Json::parse(Read(path), nullptr, false);
  Require(document.is_object(), "Invalid shared identity document");
  return document;
}
Json PrivateDocument(const std::filesystem::path& path) {
  auto document = Json::parse(private_file::Read(path), nullptr, false);
  Require(document.is_object(), "Invalid private identity document");
  return document;
}
void WriteMachine(const wchar_t* name, const std::string& contents) {
  const auto root = UnattendedDataPath();
  Require(unattended_detail::WritePrivateMachineFile(root, root / name, contents),
          "Cannot persist machine identity");
}
HANDLE Lock(const wchar_t* name) {
  const auto root = UnattendedDataPath();
  return unattended_detail::OpenPrivateMachineFile(root,
      root / name, GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS);
}
std::string Hex(const std::string& bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (unsigned char byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}
std::string Unhex(const std::string& text) {
  Require(text.size() % 2 == 0, "Invalid identity cache encoding");
  const auto nibble = [](char c) -> unsigned {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    throw std::runtime_error("Invalid identity cache encoding");
  };
  std::string bytes;
  for (size_t i = 0; i < text.size(); i += 2)
    bytes += static_cast<char>((nibble(text[i]) << 4) | nibble(text[i + 1]));
  return bytes;
}
credential_cache::CacheV3 ValidateCache(const std::string& bytes) {
  Require(bytes.size() == sizeof(credential_cache::CacheV3), "Missing valid v3 identity cache");
  credential_cache::CacheV3 cache{};
  std::memcpy(&cache, bytes.data(), sizeof(cache));
  Require(credential_cache::Valid(cache), "Invalid identity cache checksum");
  const auto valid = [](const char* identity) {
    if (!identity[0]) return true;
    if (identity[16] || identity[9] != '@') return false;
    for (int i = 0; i < 9; ++i) if (identity[i] < '0' || identity[i] > '9') return false;
    for (int i = 10; i < 16; ++i) if (!identity[i]) return false;
    return true;
  };
  Require(valid(cache.base.client_id_with_password) && valid(cache.base.self_hosted_id) &&
      valid(cache.pending_identity) &&
      (cache.base.client_id_with_password[0] || cache.base.self_hosted_id[0]),
      "Connect once to obtain a device ID before enabling unattended startup");
  return cache;
}
void PreserveLocalCipher(const std::filesystem::path& directory,
                         credential_cache::CacheV3& destination, bool gui) {
  bool existing = false;
  for (const char* name : {"secure_cache_v3.enc", "secure_cache_v2.enc", "secure_cache.enc"}) {
    const auto path = directory / name;
    if (!std::filesystem::exists(path)) continue;
    existing = true;
    const auto bytes = Read(path);
    if (std::strcmp(name, "secure_cache_v3.enc") == 0 &&
        bytes.size() == sizeof(credential_cache::CacheV3)) {
      credential_cache::CacheV3 local{};
      std::memcpy(&local, bytes.data(), sizeof(local));
      if (!credential_cache::Valid(local)) continue;
      std::memcpy(destination.base.key, local.base.key, sizeof(local.base.key));
      std::memcpy(destination.base.iv, local.base.iv, sizeof(local.base.iv));
      return;
    }
    if (std::strcmp(name, "secure_cache_v2.enc") == 0 &&
        bytes.size() == sizeof(credential_cache::CacheV2)) {
      credential_cache::CacheV2 local{};
      std::memcpy(&local, bytes.data(), sizeof(local));
      std::memcpy(destination.base.key, local.key, sizeof(local.key));
      std::memcpy(destination.base.iv, local.iv, sizeof(local.iv));
      return;
    }
    if (std::strcmp(name, "secure_cache.enc") == 0 &&
        bytes.size() == sizeof(credential_cache::CacheV1)) {
      credential_cache::CacheV1 local{};
      std::memcpy(&local, bytes.data(), sizeof(local));
      std::memcpy(destination.base.key, local.key, sizeof(local.key));
      std::memcpy(destination.base.iv, local.iv, sizeof(local.iv));
      return;
    }
  }
  Require(!gui || !existing, "Cannot preserve this user's saved-connection encryption keys");
  Require(RAND_bytes(destination.base.key, sizeof(destination.base.key)) == 1 &&
          RAND_bytes(destination.base.iv, sizeof(destination.base.iv)) == 1,
          "Cannot create local cache encryption keys");
}
void ValidateTrustedDevices(const Json& devices) {
  Require(devices.at("version") == 2 && !devices.contains("private_key") &&
          devices.at("devices").is_array() && devices.at("devices").size() <= 128,
          "Invalid shared trusted device store");
  std::set<std::string> keys;
  for (const auto& device : devices.at("devices")) {
    const auto key = device.at("public_key").get<std::string>();
    Require(Unhex(key).size() == 32 && keys.insert(key).second,
            "Invalid shared trusted device key");
    for (const char* field : {"name", "device_id"}) {
      const auto label = device.at(field).get<std::string>();
      Require(label.size() <= 128 && std::none_of(label.begin(), label.end(),
                  [](unsigned char c) { return c < 32 || c == 127; }),
              "Invalid shared trusted device label");
    }
  }
}
Json Snapshot() {
  Require(PrepareUnattendedDirectory(false), "Machine identity directory is not private");
  auto data = Document(UnattendedDataPath() / kSnapshot);
  Require(data.at("version") == 1 && data.at("host_config").is_string(),
          "Unsupported shared identity version");
  ValidateCache(Unhex(data.at("cache_v3").get<std::string>()));
  Require(Unhex(data.at("trust_private_key").get<std::string>()).size() == 32,
          "Invalid shared device identity key");
  ValidateTrustedDevices(data.at("trusted_devices"));
  return data;
}
bool SameOwner(const Json& owner) {
  return owner.value("pid", 0u) == GetCurrentProcessId() &&
         owner.value("created", uint64_t{0}) == Created(GetCurrentProcess());
}
void RemoveGuiOwner() {
  try {
    if (SameOwner(Document(UnattendedDataPath() / kGuiOwner)))
      DeleteFileW((UnattendedDataPath() / kGuiOwner).c_str());
  } catch (...) {}
}
}  // namespace

SharedHostIdentity::SharedHostIdentity(std::filesystem::path cache,
    std::filesystem::path trusted, HANDLE lease, bool gui)
    : cache_(std::move(cache)), trusted_(std::move(trusted)), lease_(lease), gui_(gui) {}

SharedHostIdentity::~SharedHostIdentity() {
  if (gui_) RemoveGuiOwner();
  if (lease_ != INVALID_HANDLE_VALUE) CloseHandle(lease_);
}

bool SharedHostIdentity::Configured() {
  const auto path = UnattendedDataPath() / kSnapshot;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES;
}

bool SharedHostIdentity::GuiPresent() {
  try {
    if (!PrepareUnattendedDirectory(false)) return false;
    const auto owner = Document(UnattendedDataPath() / kGuiOwner);
    Handle process{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                FALSE, owner.at("pid").get<DWORD>())};
    if (!process.value || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT ||
        !Created(process.value) || Created(process.value) != owner.at("created")) return false;
    DWORD session = 0;
    return ProcessIdToSessionId(owner.at("pid").get<DWORD>(), &session) && session != 0 &&
        session == owner.at("session").get<DWORD>() &&
        std::filesystem::path(ProcessPath(process.value)).u8string() == owner.at("executable");
  } catch (...) { return false; }
}

std::unique_ptr<SharedHostIdentity> SharedHostIdentity::AttachGui(
    const std::filesystem::path& cache, const std::filesystem::path& trusted,
    bool import_existing, std::string& error) {
  error.clear();
  bool reserved = false;
  try {
    Require(IsAdministratorProcess() && !IsLocalSystemProcess(), "Shared GUI identity requires an administrator");
    Require(PrepareUnattendedDirectory(true), "Cannot prepare private machine identity directory");
    Handle claim{Lock(L"gui-claim.lock")};
    Require(claim.value != INVALID_HANDLE_VALUE, "Another GUI attachment is in progress");
    Require(!GuiPresent(), "Another GUI is already using this machine identity");
    DWORD session = 0;
    Require(ProcessIdToSessionId(GetCurrentProcessId(), &session) && session != 0,
            "A GUI identity requires an interactive session");
    WriteMachine(kGuiOwner, Json{{"pid", GetCurrentProcessId()}, {"created", Created(GetCurrentProcess())},
        {"session", session}, {"executable", std::filesystem::path(ProcessPath(GetCurrentProcess())).u8string()}}.dump());
    reserved = true;
    // The GUI imports a profile, but never acquires the host's network lease.
    // Keeping the lease in SYSTEM is what preserves RTC across console login.
    auto result = std::unique_ptr<SharedHostIdentity>(
        new SharedHostIdentity(cache, trusted, INVALID_HANDLE_VALUE, true));
    if (import_existing) result->Import();
    else result->host_config_ = Snapshot().at("host_config").get<std::string>();
    return result;
  } catch (const std::exception& failure) {
    if (reserved) RemoveGuiOwner();
    error = failure.what();
    return nullptr;
  }
}

std::unique_ptr<SharedHostIdentity> SharedHostIdentity::AcquireHost(std::string& error) {
  error.clear();
  try {
    Require(IsLocalSystemProcess() && PrepareUnattendedDirectory(false), "Machine identity requires SYSTEM");
    HANDLE lease = Lock(L"identity-owner.lock");
    Require(lease != INVALID_HANDLE_VALUE, "Machine identity is already in use");
    const auto cache = UnattendedDataPath() / L"host";
    auto result = std::unique_ptr<SharedHostIdentity>(
        new SharedHostIdentity(cache, cache / L"trusted-devices", lease, false));
    result->Import();
    private_file::Write(cache / "config.ini", result->host_config_);
    return result;
  } catch (const std::exception& failure) { error = failure.what(); return nullptr; }
}

void SharedHostIdentity::Import() {
  const auto data = Snapshot();
  private_file::EnsureDirectory(cache_, true);
  private_file::EnsureDirectory(trusted_);
  Json identity{{"version", 1}};
  if (std::filesystem::exists(trusted_ / "identity.json"))
    identity = PrivateDocument(trusted_ / "identity.json");
  else if (std::filesystem::exists(cache_ / "identity.json"))
    identity = PrivateDocument(cache_ / "identity.json");
  Require(identity.is_object() && identity.value("version", 0) == 1,
          "Invalid existing device identity store");
  identity["trust_private_key"] = data.at("trust_private_key");
  const auto bytes = Unhex(data.at("cache_v3").get<std::string>());
  credential_cache::CacheV3 cache{};
  std::memcpy(&cache, bytes.data(), sizeof(cache));
  // These keys encrypt local thumbnails/recent credentials, not the device
  // identity. Sharing them would make another user's saved history unreadable.
  PreserveLocalCipher(cache_, cache, gui_);
  cache.checksum = credential_cache::Checksum(&cache, offsetof(credential_cache::CacheV3, checksum));
  // Validate everything before replacing any user identity data. Both stores
  // stay private to this process's user; their DACLs are never relaxed.
  private_file::Write(trusted_ / "identity.json", identity.dump());
  private_file::Write(trusted_ / "devices.json", data.at("trusted_devices").dump());
  private_file::Write(cache_ / "secure_cache_v3.enc",
                      std::string(reinterpret_cast<const char*>(&cache), sizeof(cache)));
  private_file::Write(cache_ / "secure_cache_v2.enc",
                      std::string(reinterpret_cast<const char*>(&cache.base), sizeof(cache.base)));
  host_config_ = data.at("host_config").get<std::string>();
}

bool SharedHostIdentity::Publish(const std::string& host_config, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
  try {
    Require(!gui_ && lease_ != INVALID_HANDLE_VALUE, "Machine identity lease is unavailable");
    auto cache = ValidateCache(Read(cache_ / "secure_cache_v3.enc"));
    std::memset(cache.base.key, 0, sizeof(cache.base.key));
    std::memset(cache.base.iv, 0, sizeof(cache.base.iv));
    cache.checksum = credential_cache::Checksum(&cache, offsetof(credential_cache::CacheV3, checksum));
    const std::string identity_cache(reinterpret_cast<const char*>(&cache), sizeof(cache));
    // CLI setup can publish an installation that has not run GUI migration yet.
    const auto identity = PrivateDocument(
        std::filesystem::exists(trusted_ / "identity.json")
            ? trusted_ / "identity.json" : cache_ / "identity.json");
    const auto key = identity.at("trust_private_key").get<std::string>();
    Require(Unhex(key).size() == 32, "Invalid device identity key");
    const auto devices = PrivateDocument(trusted_ / "devices.json");
    ValidateTrustedDevices(devices);
    const auto& configuration = host_config.empty() ? host_config_ : host_config;
    Require(!configuration.empty(), "Shared host configuration is missing");
    Json snapshot{{"version", 1}, {"cache_v3", Hex(identity_cache)}, {"trust_private_key", key},
                  {"trusted_devices", devices}, {"host_config", configuration}};
    const auto contents = snapshot.dump();
    Require(contents.size() <= 256 * 1024, "Shared identity is too large");
    WriteMachine(kSnapshot, contents);
    host_config_ = configuration;
    return true;
  } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool SharedHostIdentity::Prepare(const std::filesystem::path& cache,
    const std::filesystem::path& trusted, const std::string& host_config, std::string& error) {
  error.clear();
  try {
    Require(PrepareUnattendedDirectory(true), "Cannot prepare private machine identity directory");
    if (Configured()) { Snapshot(); return true; }
    Require(!GuiPresent(), "Enable shared startup from the running GUI settings");
    HANDLE lease = Lock(L"identity-owner.lock");
    Require(lease != INVALID_HANDLE_VALUE, "Machine identity is already in use");
    SharedHostIdentity snapshot(cache, trusted, lease, false);
    return snapshot.Publish(host_config, error);
  } catch (const std::exception& failure) { error = failure.what(); return false; }
}

}  // namespace crossdesk
