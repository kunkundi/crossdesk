#include "mobile_device_trust.h"

#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace crossdesk::mobile_controller {
// The platform must durably save the seed in its secure store before using it.
// A missing seed may be generated on first use; a malformed one must never be
// replaced automatically, since that would invalidate existing pairings.
class DeviceTrustIdentity::Impl {
 public:
  explicit Impl(std::string_view seed) {
    Require(IsHex(seed, 32));
    Secret bytes;
    for (size_t i = 0; i < bytes.value.size(); ++i)
      bytes.value[i] = (Digit(seed[2 * i]) << 4) | Digit(seed[2 * i + 1]);
    key_.reset(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                           bytes.value.data(), bytes.value.size()));
    Require(bool(key_));
    std::array<unsigned char, 32> public_key{};
    size_t size = public_key.size();
    Require(EVP_PKEY_get_raw_public_key(key_.get(), public_key.data(), &size) == 1 && size == 32);
    public_key_ = Hex(public_key.data(), public_key.size());
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
    Require(SHA256(public_key.data(), public_key.size(), hash.data()) != nullptr);
    fingerprint_ = Hex(hash.data(), hash.size());
  }

  static std::string GeneratePrivateKey() {
    Key key(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519"), EVP_PKEY_free);
    Require(bool(key));
    Secret bytes;
    size_t size = bytes.value.size();
    Require(EVP_PKEY_get_raw_private_key(key.get(), bytes.value.data(), &size) == 1 && size == 32);
    return Hex(bytes.value.data(), bytes.value.size());
  }

  std::string PairingInfo(const std::string& device_id, const std::string& name) const {
    Require(Label(device_id) && Label(name));
    return nlohmann::json{{"type", "crossdesk-pairing"}, {"version", 1},
        {"public_key", public_key_}, {"device_id", device_id}, {"name", name}}.dump();
  }

  const std::string& Fingerprint() const { return fingerprint_; }

  // nullopt: ordinary control message; empty: unsupported/malformed challenge.
  // No peer-controlled parse or crypto exception may escape a data callback.
  std::optional<std::string> ChallengeResponse(std::string_view data,
      const std::string& device_id, const std::string& name) const noexcept {
    if (data.empty() || data.size() > 2048) return std::nullopt;
    try {
      auto message = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
      if (!message.is_object() || !message.contains("type") ||
          message["type"] != "device_trust_challenge") return std::nullopt;
      if (!message.at("version").is_number_integer() || message.at("version") != 1 ||
          !message.at("nonce").is_string()) return std::string{};
      const auto nonce = message.at("nonce").get<std::string>();
      if (!IsHex(nonce, 32) || !Label(device_id) || !Label(name)) return std::string{};
      const auto transcript = nlohmann::json::array({"crossdesk.device-trust.v1", nonce,
          public_key_, device_id, name}).dump();
      std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
      std::array<unsigned char, 64> signature{};
      size_t size = signature.size();
      Require(context && EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1 &&
              EVP_DigestSign(context.get(), signature.data(), &size,
                  reinterpret_cast<const unsigned char*>(transcript.data()), transcript.size()) == 1 && size == 64);
      return nlohmann::json{{"type", "device_trust_proof"}, {"version", 1}, {"nonce", nonce},
          {"public_key", public_key_}, {"device_id", device_id}, {"name", name},
          {"signature", Hex(signature.data(), signature.size())}}.dump();
    } catch (const std::exception&) { return std::string{}; }
  }

 private:
  using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
  struct Secret {
    std::array<unsigned char, 32> value{};
    ~Secret() { OPENSSL_cleanse(value.data(), value.size()); }
  };
  static void Require(bool ok) {
    if (!ok) throw std::runtime_error("Device signing identity unavailable");
  }
  static int Digit(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }
  static bool IsHex(std::string_view value, size_t bytes) {
    return value.size() == bytes * 2 && std::all_of(value.begin(), value.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
  }
  static std::string Hex(const unsigned char* bytes, size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
      result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15];
    }
    return result;
  }
  static bool Label(const std::string& value) {
    return value.size() <= 128 && std::none_of(value.begin(), value.end(),
        [](unsigned char c) { return c < 32 || c == 127; });
  }
  Key key_{nullptr, EVP_PKEY_free};
  std::string public_key_, fingerprint_;
};
DeviceTrustIdentity::DeviceTrustIdentity(std::string_view seed) : impl_(std::make_unique<Impl>(seed)) {}
DeviceTrustIdentity::~DeviceTrustIdentity() = default;
std::string DeviceTrustIdentity::GeneratePrivateKey() { return Impl::GeneratePrivateKey(); }
std::string DeviceTrustIdentity::PairingInfo(const std::string& id, const std::string& name) const {
  return impl_->PairingInfo(id, name);
}
const std::string& DeviceTrustIdentity::Fingerprint() const { return impl_->Fingerprint(); }
std::optional<std::string> DeviceTrustIdentity::ChallengeResponse(std::string_view data,
    const std::string& id, const std::string& name) const noexcept {
  return impl_->ChallengeResponse(data, id, name);
}
std::string DeviceTrustIdentity::Advertise(std::string_view host_information) {
  auto info = nlohmann::json::parse(host_information.begin(), host_information.end());
  info["device_trust_version"] = 1;
  return info.dump();
}
void DeviceTrustIdentity::ClearSecret(std::string& value) {
  OPENSSL_cleanse(value.data(), value.size());
  value.clear();
}
bool DeviceTrustIdentity::IsChallenge(std::string_view data) noexcept {
  if (data.empty() || data.size() > 2048) return false;
  try {
    const auto message = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
    return message.is_object() && message.contains("type") && message["type"] == "device_trust_challenge";
  } catch (const std::exception&) { return false; }
}
}  // namespace crossdesk::mobile_controller
