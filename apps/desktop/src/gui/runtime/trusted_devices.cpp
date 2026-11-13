#include "runtime/trusted_devices.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <stdexcept>

#include "private_file.h"
#include "runtime/device_identity_store.h"

namespace crossdesk {
namespace {
constexpr size_t kMaxTrustedDevices = 128;
void Require(bool ok, const char* error = "Invalid or unavailable trusted device identity") {
  if (!ok) throw std::runtime_error(error);
}
std::string Hex(const uint8_t* data, size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    result += digits[data[i] >> 4]; result += digits[data[i] & 15];
  }
  return result;
}
bool IsHex(const std::string& value, size_t bytes) {
  return value.size() == bytes * 2 && std::all_of(value.begin(), value.end(),
      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::vector<uint8_t> Unhex(const std::string& text, size_t bytes) {
  Require(IsHex(text, bytes));
  auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  std::vector<uint8_t> result(bytes);
  for (size_t i = 0; i < bytes; ++i)
    result[i] = (digit(text[2 * i]) << 4) | digit(text[2 * i + 1]);
  return result;
}
bool Label(const std::string& value) {
  return value.size() <= 128 && std::none_of(value.begin(), value.end(),
      [](unsigned char c) { return c < 32 || c == 127; });
}
std::string Transcript(const std::string& nonce, const TrustedDevices::Device& device) {
  // Canonical encoding and a domain tag prevent ambiguous or cross-protocol use.
  return nlohmann::json::array({"crossdesk.device-trust.v1", nonce, device.public_key,
      device.device_id, device.name}).dump();
}
using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
struct PrivateText {
  std::string value;
  ~PrivateText() { OPENSSL_cleanse(value.data(), value.size()); }
};
}

TrustedDevices::TrustedDevices(const std::filesystem::path& directory,
                               const std::filesystem::path& cache,
                               bool require_existing)
    : path_(directory / "devices.json") {
  private_file::EnsureDirectory(directory);
  const bool exists = std::filesystem::symlink_status(path_).type() !=
                      std::filesystem::file_type::not_found;
  Require(exists || !require_existing,
          "Trusted device list is missing while restrictions are enabled; restore the original configuration");
  PrivateText legacy_key;
  if (exists) {
    PrivateText text{private_file::Read(path_)};
    auto saved = nlohmann::json::parse(text.value, nullptr, false);
    Require(!saved.is_discarded());
    const bool legacy = saved.at("version") == 1;
    Require((legacy || saved.at("version") == 2) && saved.at("devices").is_array() &&
            saved.at("devices").size() <= kMaxTrustedDevices);
    if (legacy) {
      legacy_key.value = saved.at("private_key").get<std::string>();
      auto& encoded = saved["private_key"].get_ref<std::string&>();
      OPENSSL_cleanse(encoded.data(), encoded.size());
      Require(IsHex(legacy_key.value, 32));
    } else Require(!saved.contains("private_key"));
    for (const auto& row : saved.at("devices")) {
      Device device{row.at("public_key"), row.at("name"), row.at("device_id")};
      Require(IsHex(device.public_key, 32) && Label(device.name) && Label(device.device_id));
      Require(devices_.emplace(device.public_key, device).second);
    }
  }

  device_identity_store::Update(directory, [&](nlohmann::json& identity, bool identity_exists) {
    bool legacy_identity_exists = false;
    if (!identity_exists) {
      // The persistent copy is authoritative once migration has completed.
      // Leave the old cache copy intact so an interrupted migration is retryable.
      if (auto previous = device_identity_store::Read(cache)) {
        identity = std::move(*previous);
        legacy_identity_exists = true;
      }
    } else {
      Require(identity.contains("trust_private_key"),
              "Persistent device signing key is missing; restore the original identity");
    }
    // Only a never-completed first initialization may create an empty list.
    // Older identity stores predate this marker and must already have a list.
    Require(exists || (!legacy_identity_exists &&
                (!identity_exists || !identity.value("trusted_devices_initialized", true))),
            "Trusted device list is missing from an existing installation; restore the original list");
    if (!identity.contains("trust_private_key") && !legacy_key.value.empty())
      identity["trust_private_key"] = legacy_key.value;
    if (identity.contains("trust_private_key")) {
      const auto& encoded = identity.at("trust_private_key").get_ref<const std::string&>();
      // Interrupted migration may leave both copies. Never replace either key
      // on a mismatch: that would silently invalidate existing pairings.
      Require(legacy_key.value.empty() || encoded == legacy_key.value);
      auto secret = Unhex(encoded, 32);
      key_.reset(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                             secret.data(), secret.size()));
      OPENSSL_cleanse(secret.data(), secret.size());
    } else {
      Require(!exists && !legacy_identity_exists,
              "Device signing key is missing from an existing installation; restore the original identity");
      key_.reset(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519"));
      Require(bool(key_));
      std::array<uint8_t, 32> secret{};
      size_t size = secret.size();
      Require(EVP_PKEY_get_raw_private_key(key_.get(), secret.data(), &size) == 1 && size == 32);
      identity["trust_private_key"] = Hex(secret.data(), secret.size());
      OPENSSL_cleanse(secret.data(), secret.size());
    }
    Require(bool(key_));
    identity["trusted_devices_initialized"] = exists;
  });

  std::array<uint8_t, 32> public_key{};
  size_t size = public_key.size();
  Require(EVP_PKEY_get_raw_public_key(key_.get(), public_key.data(), &size) == 1 && size == 32);
  public_key_ = Hex(public_key.data(), public_key.size());
  // The identity update is durable before removing the legacy secret. If this
  // write fails, reopening can safely retry with the same key and device list.
  if (!exists || !legacy_key.value.empty()) Save(devices_);
  if (!exists) {
    device_identity_store::Update(directory, [](nlohmann::json& identity, bool present) {
      Require(present && identity.contains("trust_private_key"),
              "Device identity disappeared during initialization");
      identity["trusted_devices_initialized"] = true;
    });
  }
}

std::string TrustedDevices::Fingerprint(const std::string& public_key) {
  const auto bytes = Unhex(public_key, 32);
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  SHA256(bytes.data(), bytes.size(), hash.data());
  return Hex(hash.data(), hash.size());
}
std::string TrustedDevices::Fingerprint() const { return Fingerprint(public_key_); }

std::string TrustedDevices::PairingInfo(const std::string& device_id,
                                       const std::string& name) const {
  Require(Label(device_id) && Label(name));
  // Export public information only. Connection admission still requires a fresh
  // proof of private-key possession; importing this document grants no bypass.
  return nlohmann::json{{"type", "crossdesk-pairing"}, {"version", 1},
      {"public_key", public_key_}, {"device_id", device_id}, {"name", name}}.dump();
}

std::optional<TrustedDevices::Device> TrustedDevices::ParsePairingInfo(std::string_view text) {
  if (text.empty() || text.size() > kMaxPairingInfoSize) return std::nullopt;
  try {
    const auto info = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (!info.is_object() || info.size() != 5 || info.at("type") != "crossdesk-pairing" ||
        !info.at("version").is_number_integer() || info.at("version") != 1)
      return std::nullopt;
    Device device{info.at("public_key"), info.at("name"), info.at("device_id")};
    if (IsHex(device.public_key, 32) && Label(device.name) && Label(device.device_id))
      return device;
  } catch (const std::exception&) {}
  return std::nullopt;
}

std::string TrustedDevices::Nonce() {
  std::array<uint8_t, 32> bytes{};
  Require(RAND_bytes(bytes.data(), bytes.size()) == 1);
  return Hex(bytes.data(), bytes.size());
}

nlohmann::json TrustedDevices::Prove(const std::string& nonce,
    const std::string& device_id, const std::string& name) const {
  Require(IsHex(nonce, 32) && Label(device_id) && Label(name));
  const Device device{public_key_, name, device_id};
  const auto text = Transcript(nonce, device);
  return {{"type", "device_trust_proof"}, {"version", 1}, {"nonce", nonce},
          {"public_key", public_key_}, {"device_id", device_id}, {"name", name},
          {"signature", Sign(text)}};
}

std::string TrustedDevices::Sign(std::string_view text) const {
  Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  std::array<uint8_t, 64> signature{};
  size_t size = signature.size();
  Require(context && EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1 &&
          EVP_DigestSign(context.get(), signature.data(), &size,
                         reinterpret_cast<const uint8_t*>(text.data()), text.size()) == 1 && size == 64);
  return Hex(signature.data(), signature.size());
}

std::optional<TrustedDevices::Device> TrustedDevices::Verify(
    const nlohmann::json& proof, const std::string& nonce) {
  try {
    if (proof.at("version") != 1 || proof.at("nonce") != nonce || !IsHex(nonce, 32))
      return std::nullopt;
    Device device{proof.at("public_key"), proof.at("name"), proof.at("device_id")};
    if (!Label(device.name) || !Label(device.device_id)) return std::nullopt;
    const auto public_key = Unhex(device.public_key, 32);
    const auto signature = Unhex(proof.at("signature"), 64);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, public_key.data(), public_key.size()),
        EVP_PKEY_free);
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    const auto text = Transcript(nonce, device);
    if (key && context && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
        EVP_DigestVerify(context.get(), signature.data(), signature.size(),
                         reinterpret_cast<const uint8_t*>(text.data()), text.size()) == 1)
      return device;
  } catch (const std::exception&) {}
  return std::nullopt;
}

bool TrustedDevices::Contains(const std::string& public_key) const {
  std::lock_guard lock(mutex_);
  return devices_.count(public_key) != 0;
}
std::vector<TrustedDevices::Device> TrustedDevices::List() const {
  std::lock_guard lock(mutex_);
  std::vector<Device> result;
  for (const auto& [key, device] : devices_) result.push_back(device);
  return result;
}
void TrustedDevices::Save(const std::map<std::string, Device>& devices) const {
  nlohmann::json saved{{"version", 2}, {"devices", nlohmann::json::array()}};
  for (const auto& [key, device] : devices)
    saved["devices"].push_back({{"public_key", key}, {"name", device.name}, {"device_id", device.device_id}});
  const auto text = saved.dump(2);
  Require(text.size() <= 64 * 1024);
  private_file::Write(path_, text);
}
void TrustedDevices::Add(const Device& device) {
  Require(IsHex(device.public_key, 32) && Label(device.name) && Label(device.device_id));
  std::lock_guard lock(mutex_);
  const auto existing = devices_.find(device.public_key);
  if (existing != devices_.end() && existing->second == device) return;
  Require(existing != devices_.end() || devices_.size() < kMaxTrustedDevices);
  auto next = devices_;
  next[device.public_key] = device;
  Save(next);
  devices_.swap(next);
}
void TrustedDevices::Remove(const std::string& public_key) {
  std::lock_guard lock(mutex_);
  if (!devices_.contains(public_key)) return;
  auto next = devices_;
  next.erase(public_key);
  Save(next);
  devices_.swap(next);
}
void TrustedDevices::Replace(const std::vector<Device>& devices) {
  Require(devices.size() <= kMaxTrustedDevices);
  std::map<std::string, Device> next;
  for (const auto& device : devices) {
    Require(IsHex(device.public_key, 32) && Label(device.name) && Label(device.device_id));
    Require(next.emplace(device.public_key, device).second);
  }
  std::lock_guard lock(mutex_);
  if (next == devices_) return;
  Save(next);
  devices_.swap(next);
}
}  // namespace crossdesk
