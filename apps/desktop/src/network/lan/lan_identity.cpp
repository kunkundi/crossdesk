#include "lan_identity.h"
#include "private_file.h"

#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "direct_address.h"
#include "lan_password.h"
#ifdef _WIN32
// Winsock must precede windows.h; the networking headers need its types.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef __APPLE__
#include <net/if_dl.h>
#include <net/if_types.h>
#endif
#endif

namespace crossdesk::lan {
namespace {
void Require(bool ok, const char* error) {
  if (!ok) throw std::runtime_error(error);
}
std::string Hex(const unsigned char* data, size_t size) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(size * 2, '0');
  for (size_t i = 0; i < size; ++i) {
    result[2 * i] = digits[data[i] >> 4];
    result[2 * i + 1] = digits[data[i] & 15];
  }
  return result;
}
bool IsHex(const std::string& text, size_t size) {
  return text.size() == size &&
         std::all_of(text.begin(), text.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
using private_file::Read;
using private_file::Write;
std::string BioText(BIO* bio) {
  char* data = nullptr;
  long size = BIO_get_mem_data(bio, &data);
  Require(size > 0, "Cannot encode LAN identity");
  return {data, static_cast<size_t>(size)};
}
struct Secret {
  std::string value;
  ~Secret() { OPENSSL_cleanse(value.data(), value.size()); }
};
std::string Unhex(const std::string& value, size_t bytes) {
  Require(IsHex(value, bytes * 2), "Invalid saved LAN password");
  std::string result(bytes, '\0');
  auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (size_t i = 0; i < bytes; ++i)
    result[i] =
        static_cast<char>((digit(value[2 * i]) << 4) | digit(value[2 * i + 1]));
  return result;
}
std::string PasswordKey(const std::filesystem::path& directory, bool create) {
  const auto path = directory / "password-key";
  if (create && !std::filesystem::exists(path)) {
    Secret key{std::string(32, '\0')};
    Require(
        RAND_bytes(reinterpret_cast<unsigned char*>(key.value.data()), 32) == 1,
        "Cannot generate LAN password storage key");
    Write(path, key.value);
  }
  auto key = Read(path);
  Require(key.size() == 32, "Invalid LAN password storage key");
  return key;
}
// The independent local key and credential files use owner-only permissions.
// Encryption avoids plaintext in identity.json; it does not protect against
// an attacker who can read both this user's key and credential files.
nlohmann::json EncryptPassword(const std::string& password,
                               const std::string& key,
                               const std::string& context) {
  const auto nonce = Unhex(RandomHex(12), 12);
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> cipher(
      EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  std::array<unsigned char, 32> data{}, tag{};
  int size = 0, tail = 0;
  Require(
      cipher &&
          EVP_EncryptInit_ex(
              cipher.get(), EVP_aes_256_gcm(), nullptr,
              reinterpret_cast<const unsigned char*>(key.data()),
              reinterpret_cast<const unsigned char*>(nonce.data())) == 1 &&
          EVP_EncryptUpdate(
              cipher.get(), nullptr, &size,
              reinterpret_cast<const unsigned char*>(context.data()),
              static_cast<int>(context.size())) == 1 &&
          EVP_EncryptUpdate(
              cipher.get(), data.data(), &size,
              reinterpret_cast<const unsigned char*>(password.data()),
              static_cast<int>(password.size())) == 1 &&
          EVP_EncryptFinal_ex(cipher.get(), data.data() + size, &tail) == 1 &&
          EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_GET_TAG, 16,
                              tag.data()) == 1,
      "Cannot save LAN password");
  return {{"version", 1},
          {"algorithm", "aes-256-gcm"},
          {"nonce", Hex(reinterpret_cast<const unsigned char*>(nonce.data()),
                        nonce.size())},
          {"data", Hex(data.data(), size + tail)},
          {"tag", Hex(tag.data(), 16)}};
}
std::string DecryptPassword(const nlohmann::json& record,
                            const std::string& key,
                            const std::string& context) {
  Require(record.at("version") == 1 && record.at("algorithm") == "aes-256-gcm",
          "Invalid saved LAN password");
  const auto nonce = Unhex(record.at("nonce"), 12);
  const auto data = Unhex(record.at("data"), 6);
  auto tag = Unhex(record.at("tag"), 16);
  Secret plain{std::string(32, '\0')};
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> cipher(
      EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  int size = 0, tail = 0;
  Require(cipher &&
              EVP_DecryptInit_ex(
                  cipher.get(), EVP_aes_256_gcm(), nullptr,
                  reinterpret_cast<const unsigned char*>(key.data()),
                  reinterpret_cast<const unsigned char*>(nonce.data())) == 1 &&
              EVP_DecryptUpdate(
                  cipher.get(), nullptr, &size,
                  reinterpret_cast<const unsigned char*>(context.data()),
                  static_cast<int>(context.size())) == 1 &&
              EVP_DecryptUpdate(
                  cipher.get(),
                  reinterpret_cast<unsigned char*>(plain.value.data()), &size,
                  reinterpret_cast<const unsigned char*>(data.data()),
                  static_cast<int>(data.size())) == 1 &&
              EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_SET_TAG, 16,
                                  tag.data()) == 1 &&
              EVP_DecryptFinal_ex(
                  cipher.get(),
                  reinterpret_cast<unsigned char*>(plain.value.data()) + size,
                  &tail) == 1 &&
              size + tail == 6,
          "Cannot read saved LAN password");
  auto password = plain.value.substr(0, 6);
  Require(ValidPassword(password), "Invalid saved LAN password");
  return password;
}
void CreateCertificate(const std::filesystem::path& key_path,
                       const std::filesystem::path& cert_path) {
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
  std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
  Require(key && cert, "Cannot generate LAN identity");
  X509_set_version(cert.get(), 2);
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
  X509_gmtime_adj(X509_getm_notBefore(cert.get()), -300);
  X509_gmtime_adj(X509_getm_notAfter(cert.get()), 10L * 365 * 86400);
  X509_set_pubkey(cert.get(), key.get());
  auto* name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(
      name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char*>("CrossDesk LAN"), -1, -1, 0);
  X509_set_issuer_name(cert.get(), name);
  for (const auto& extension :
       {std::pair<int, const char*>{NID_basic_constraints, "critical,CA:FALSE"},
        {NID_key_usage, "critical,digitalSignature"},
        {NID_ext_key_usage, "serverAuth"}}) {
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(
        nullptr, nullptr, extension.first, const_cast<char*>(extension.second));
    Require(ext != nullptr, "Cannot create certificate extension");
    const int result = X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);
    Require(result == 1, "Cannot install certificate extension");
  }
  Require(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0,
          "Cannot sign LAN certificate");
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  Require(PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0,
                                   nullptr, nullptr) == 1,
          "Cannot encode private key");
  Write(key_path, BioText(bio.get()));
  (void)BIO_reset(bio.get());
  Require(PEM_write_bio_X509(bio.get(), cert.get()) == 1,
          "Cannot encode certificate");
  Write(cert_path, BioText(bio.get()));
}
}  // namespace
unsigned ParsePort(const std::string& value) {
  unsigned port = 0;
  auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), port);
  Require(error == std::errc{} && end == value.data() + value.size() &&
              port > 0 && port <= 65535,
          "Invalid LAN port");
  return port;
}
bool IsIpAddress(const std::string& value) {
  return minirtc::ParseDirectAddress(value).has_value();
}
std::string NormalizeAddress(const std::string& value) {
  const auto address = minirtc::ParseDirectAddress(value);
  Require(address.has_value(), "Invalid LAN endpoint");
  return minirtc::DirectAddressString(*address);
}
std::string FormatEndpoint(const std::string& address, unsigned port) {
  return (address.find(':') == std::string::npos ? address
                                                 : "[" + address + "]") +
         ":" + std::to_string(port);
}
std::pair<std::string, unsigned> ParseEndpoint(const std::string& value,
                                               unsigned default_port) {
  std::string address = value;
  unsigned port = default_port;
  if (!value.empty() && value.front() == '[') {
    const auto close = value.find(']');
    Require(close != std::string::npos, "Invalid LAN endpoint");
    address = value.substr(1, close - 1);
    Require(address.find(':') != std::string::npos, "Invalid LAN endpoint");
    if (close + 1 < value.size()) {
      Require(value[close + 1] == ':', "Invalid LAN endpoint");
      port = ParsePort(value.substr(close + 2));
    }
  } else if (const auto colon = value.find(':');
             colon != std::string::npos &&
             value.find(':', colon + 1) == std::string::npos) {
    port = ParsePort(value.substr(colon + 1));
    address.resize(colon);
  }
  Require(IsIpAddress(address) && port > 0 && port <= 65535,
          "Invalid LAN endpoint");
  return {NormalizeAddress(address), port};
}
std::vector<Interface> LocalIpInterfaces(bool physical_only) {
  std::vector<Interface> result;
  const auto append = [&](const sockaddr* native, unsigned index) {
    char text[INET6_ADDRSTRLEN]{};
    std::string address;
    if (native->sa_family == AF_INET) {
      if (!inet_ntop(AF_INET,
                     &reinterpret_cast<const sockaddr_in*>(native)->sin_addr,
                     text, sizeof(text)))
        return;
      address = text;
    } else if (native->sa_family == AF_INET6) {
      auto v6 = *reinterpret_cast<const sockaddr_in6*>(native);
      if (IN6_IS_ADDR_LINKLOCAL(&v6.sin6_addr)) {
        if (!v6.sin6_scope_id) v6.sin6_scope_id = index;
#ifdef __APPLE__
        // getifaddrs may expose the BSD embedded link scope as well.
        v6.sin6_addr.s6_addr[2] = v6.sin6_addr.s6_addr[3] = 0;
#endif
      } else {
        v6.sin6_scope_id = 0;
      }
      if (!inet_ntop(AF_INET6, &v6.sin6_addr, text, sizeof(text))) return;
      address = text;
      if (v6.sin6_scope_id) address += "%" + std::to_string(v6.sin6_scope_id);
    }
    if (IsIpAddress(address)) result.push_back({NormalizeAddress(address)});
  };
#ifdef _WIN32
  ULONG bytes = 16384;
  std::vector<unsigned char> buffer(bytes);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
  ULONG code =
      GetAdaptersAddresses(AF_UNSPEC,
                           GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                               GAA_FLAG_SKIP_DNS_SERVER,
                           nullptr, adapters, &bytes);
  if (code == ERROR_BUFFER_OVERFLOW) {
    buffer.resize(bytes);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    code = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &bytes);
  }
  if (code != NO_ERROR) return result;
  for (auto* a = adapters; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp ||
        a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
      continue;
    if (physical_only) {
      if ((a->IfType != IF_TYPE_ETHERNET_CSMACD &&
           a->IfType != IF_TYPE_IEEE80211) ||
          a->PhysicalAddressLength == 0)
        continue;
      MIB_IF_ROW2 row{};
      row.InterfaceLuid = a->Luid;
      if (GetIfEntry2(&row) != NO_ERROR ||
          !row.InterfaceAndOperStatusFlags.HardwareInterface)
        continue;
    }
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (!u->Address.lpSockaddr || (u->DadState != IpDadStatePreferred &&
                                     u->DadState != IpDadStateDeprecated))
        continue;
      append(u->Address.lpSockaddr, a->Ipv6IfIndex);
    }
  }
#else
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return result;
  for (auto* a = list; a; a = a->ifa_next) {
    if (!a->ifa_addr ||
        (a->ifa_addr->sa_family != AF_INET &&
         a->ifa_addr->sa_family != AF_INET6) ||
        !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK) ||
        (physical_only && (a->ifa_flags & IFF_POINTOPOINT)))
      continue;
    bool physical = false;
#ifdef __APPLE__
    for (auto* link = list; link; link = link->ifa_next)
      if (link->ifa_addr && link->ifa_addr->sa_family == AF_LINK &&
          std::string(link->ifa_name) == a->ifa_name) {
        const auto* dl = reinterpret_cast<sockaddr_dl*>(link->ifa_addr);
        physical = dl->sdl_type == IFT_ETHER && dl->sdl_alen == 6 &&
                   std::string(a->ifa_name).rfind("en", 0) == 0;
      }
#else
    physical = std::filesystem::exists(std::filesystem::path("/sys/class/net") /
                                       a->ifa_name / "device");
#endif
    if (physical_only && !physical) continue;
    append(a->ifa_addr, if_nametoindex(a->ifa_name));
  }
  freeifaddrs(list);
#endif
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    const bool a_v6 = a.address.find(':') != std::string::npos;
    const bool b_v6 = b.address.find(':') != std::string::npos;
    return a_v6 != b_v6 ? !a_v6 : a.address < b.address;
  });
  result.erase(std::unique(result.begin(), result.end(),
                           [](const auto& a, const auto& b) {
                             return a.address == b.address;
                           }),
               result.end());
  return result;
}
std::string RandomHex(size_t bytes) {
  std::vector<unsigned char> data(bytes);
  Require(RAND_bytes(data.data(), static_cast<int>(bytes)) == 1,
          "Random generator failed");
  return Hex(data.data(), bytes);
}
bool ValidPassword(const std::string& password) {
  return password.size() == 6 &&
         password.find_first_not_of(
             "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy"
             "z") == std::string::npos;
}
bool VerifyLanCertificate(X509* cert) {
  if (!cert || !X509_get0_pubkey(cert)) return false;
  return X509_cmp_current_time(X509_get0_notBefore(cert)) < 0 &&
         X509_cmp_current_time(X509_get0_notAfter(cert)) > 0 &&
         X509_verify(cert, X509_get0_pubkey(cert)) == 1 &&
         X509_check_purpose(cert, X509_PURPOSE_SSL_SERVER, 0) == 1 &&
         (X509_get_key_usage(cert) & KU_DIGITAL_SIGNATURE) != 0;
}
Identity::Identity(std::filesystem::path directory)
    : directory_(std::move(directory)) {}
void Identity::Load() {
  DevicePassword::CheckSupport();
  private_file::EnsureDirectory(directory_);
  const auto store = directory_ / "identity.json";
  if (!std::filesystem::exists(store)) {
    Require(!std::filesystem::exists(private_key()) &&
                !std::filesystem::exists(certificate()),
            "Incomplete LAN identity; manual recovery required");
    CreateCertificate(private_key(), certificate());
    state_ = {
        {"version", 1}, {"device_id", RandomHex(16)}, {"verifier", nullptr}};
    Save(state_);
  } else
    state_ = nlohmann::json::parse(Read(store));
  Require(state_.at("version") == 1 &&
              IsHex(state_.at("device_id").get<std::string>(), 32),
          "Invalid LAN identity store");
  // Legacy trust records no longer affect connections. Drop them from memory
  // and from the next ordinary settings/credential save, without requiring a
  // migration write before an outgoing connection can authenticate.
  state_.erase("trust");
  // Migrate the single endpoint in memory; the next ordinary save persists it.
  // Ports belong to exact IP addresses, including addresses currently offline.
  auto& settings = state_["settings"];
  if (settings.is_null()) settings = nlohmann::json::object();
  Require(settings.is_object(), "Invalid LAN settings");
  Require(
      !settings.contains("listen_all") || settings["listen_all"].is_boolean(),
      "Invalid LAN listener mode");
  const auto address = settings.value("address", std::string{});
  Require(address.empty() || IsIpAddress(address),
          "Invalid LAN listener address");
  if (!settings.contains("ports")) {
    settings["ports"] = nlohmann::json::object();
    if (!address.empty())
      settings["ports"][address] = settings.contains("port")
                                       ? settings["port"]
                                       : nlohmann::json(52888u);
  }
  Require(settings["ports"].is_object(), "Invalid LAN port settings");
  // Preserve the shared port when upgrading the IPv4 wildcard to dual stack.
  if (settings["ports"].contains("0.0.0.0")) {
    if (!settings["ports"].contains(kAllInterfaces))
      settings["ports"][kAllInterfaces] = settings["ports"]["0.0.0.0"];
    settings["ports"].erase("0.0.0.0");
  }
  for (const auto& [ip, port] : settings["ports"].items())
    Require((IsIpAddress(ip) || ip == kAllInterfaces) &&
                port.is_number_integer() && port > 0 && port <= 65535,
            "Invalid LAN port settings");
  settings["address"] = address;
  settings["version"] = 2;
  settings.erase("enabled");
  settings.erase("interface");
  settings.erase("port");
  id_ = state_.at("device_id");
  auto pem = Read(certificate());
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(
      BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
  std::unique_ptr<X509, decltype(&X509_free)> cert(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
  Require(VerifyLanCertificate(cert.get()),
          "LAN certificate invalid or expired");
  auto secret = Read(private_key());
  std::unique_ptr<BIO, decltype(&BIO_free)> key_bio(
      BIO_new_mem_buf(secret.data(), static_cast<int>(secret.size())),
      BIO_free);
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr),
      EVP_PKEY_free);
  Require(key && X509_check_private_key(cert.get(), key.get()) == 1,
          "LAN private key does not match certificate");
  OPENSSL_cleanse(secret.data(), secret.size());
  if (!state_["verifier"].is_null()) {
    const auto& v = state_["verifier"];
    if (v.at("version") == 1) {
      Require(v.at("algorithm") == "scrypt-32768-8-1" &&
                  IsHex(v.at("salt"), 32) && IsHex(v.at("hash"), 64),
              "Invalid LAN password verifier");
    } else if (v.at("version") == 2) {
      // Legacy PAKE records are read only to recover the locally encrypted
      // password for migration. They are never used by the network protocol.
      Require(v.at("algorithm") == "spake2p-p256-scrypt-32768-8-1" &&
                  IsHex(v.at("salt"), 32) && IsHex(v.at("w0"), 64) &&
                  IsHex(v.at("L"), 130),
              "Invalid LAN password verifier");
    } else {
      Require(CurrentVerifier(v), "Invalid LAN password verifier");
    }
  }
  // Lifetime lockouts are obsolete; retain only expiring in-memory cooldowns.
  state_.erase("password_attempts");
}
void Identity::Save(const nlohmann::json& state) {
  Write(directory_ / "identity.json", state.dump(2));
}
std::pair<std::string, nlohmann::json> Identity::GeneratePassword() {
  constexpr std::string_view alphabet =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  constexpr auto unbiased_limit = 256 - 256 % alphabet.size();
  std::string password;
  // Match cloud password generation: unbiased ASCII alphanumeric characters,
  // regenerating all-numeric results. Existing numeric passwords remain valid.
  do {
    password.clear();
    while (password.size() < 6) {
      unsigned char byte = 0;
      Require(RAND_bytes(&byte, 1) == 1, "Cannot generate LAN password");
      if (byte < unbiased_limit) password += alphabet[byte % alphabet.size()];
    }
  } while (password.find_first_not_of("0123456789") == std::string::npos);
  return {password, RegisterPassword(password)};
}
void Identity::SetPassword(const std::string& password,
                           const nlohmann::json& verifier) {
  Require(ValidPassword(password),
          "LAN password must contain exactly 6 ASCII letters or digits");
  Require(CurrentVerifier(verifier), "Invalid LAN password verifier");
  Secret key{PasswordKey(directory_, !state_.contains("password"))};
  auto next = state_;
  next["verifier"] = verifier;
  next["password"] = EncryptPassword(
      password, key.value,
      "crossdesk.lan.password.v1:" + id_ + ":" + verifier.dump());
  Save(next);
  state_ = std::move(next);
}
std::string Identity::ReadPassword() const {
  // Verifier-only legacy records require a local password entry to migrate.
  if (!state_.contains("password")) return {};
  Secret key{PasswordKey(directory_, false)};
  return DecryptPassword(
      state_.at("password"), key.value,
      "crossdesk.lan.password.v1:" + id_ + ":" + verifier().dump());
}
nlohmann::json Identity::RegisterPassword(const std::string& password) {
  Require(ValidPassword(password),
          "LAN password must contain exactly 6 ASCII letters or digits");
  const auto record = DevicePassword::Hash(password);
  Require(record.has_value(), "Cannot derive LAN password verifier");
  return {{"version", 3}, {"salt", record->salt}, {"hash", record->hash}};
}
bool Identity::CurrentVerifier(const nlohmann::json& verifier) {
  return verifier.is_object() && verifier.value("version", 0) == 3 &&
         verifier.contains("salt") && verifier["salt"].is_string() &&
         verifier.contains("hash") && verifier["hash"].is_string() &&
         DevicePassword::IsCurrent({verifier["salt"], verifier["hash"]});
}
bool Identity::VerifyPassword(const nlohmann::json& verifier,
                              const std::string& password) {
  if (!ValidPassword(password) || !CurrentVerifier(verifier)) return false;
  return DevicePassword::Verify(
      DevicePasswordRecord{verifier["salt"], verifier["hash"]}, password);
}

void Identity::SaveSettings(const std::string& address, unsigned port) {
  Require((IsIpAddress(address) || address == kAllInterfaces) && port > 0 &&
              port <= 65535,
          "Invalid LAN listener address");
  auto next = state_;
  const auto interfaces = LocalIpInterfaces();
  const auto selected =
      std::find_if(interfaces.begin(), interfaces.end(),
                   [&](const auto& i) { return i.address == address; });
  Require(address == kAllInterfaces || selected != interfaces.end(),
          "Selected LAN interface is unavailable");
  next["settings"]["ports"][address] = port;
  Save(next);
  state_ = std::move(next);
}
unsigned Identity::PortFor(const std::string& address) const {
  return state_.at("settings").at("ports").value(address, 52888u);
}
bool Identity::HasPortFor(const std::string& address) const {
  return state_.at("settings").at("ports").contains(address);
}
void Identity::SelectAddress(const std::string& address) {
  const auto interfaces = LocalIpInterfaces(!listen_all());
  Require(std::any_of(interfaces.begin(), interfaces.end(),
                      [&](const auto& i) { return i.address == address; }),
          "Selected LAN interface is unavailable");
  if (state_["settings"].value("address", "") == address) return;
  auto next = state_;
  next["settings"]["address"] = address;
  Save(next);
  state_ = std::move(next);
}
void Identity::SaveListenerMode(bool listen_all,
                                const std::map<std::string, unsigned>& ports) {
  auto next = state_;
  next["settings"]["listen_all"] = listen_all;
  for (const auto& [address, port] : ports) {
    Require((IsIpAddress(address) || address == kAllInterfaces) && port > 0 &&
                port <= 65535,
            "Invalid LAN listener address");
    next["settings"]["ports"][address] = port;
  }
  Save(next);
  state_ = std::move(next);
}
}  // namespace crossdesk::lan
