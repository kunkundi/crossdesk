#include "features/settings/settings_manager.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "localization.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {
namespace {

constexpr uint32_t kCacheV3Magic = 0x33444358;  // "XCD3"
constexpr uint32_t kCacheV3Version = 3;

template <size_t Size>
void CopyString(char (&destination)[Size], const char *source) {
  static_assert(Size > 0);
  std::memset(destination, 0, Size);
  if (source) {
    std::strncpy(destination, source, Size - 1);
  }
}

uint32_t CacheChecksum(const void *data, size_t size) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

std::filesystem::path TemporaryPathFor(
    const std::filesystem::path &target) {
  const auto timestamp = std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count();
  const auto thread_id =
      std::hash<std::thread::id>{}(std::this_thread::get_id());
  std::filesystem::path temporary = target;
  temporary += ".tmp-" + std::to_string(timestamp) + "-" +
               std::to_string(thread_id);
  return temporary;
}

bool FlushFileToDisk(FILE *file) {
  if (!file || std::fflush(file) != 0) {
    return false;
  }
#if defined(_WIN32)
  return _commit(_fileno(file)) == 0;
#else
  return fsync(fileno(file)) == 0;
#endif
}

bool ReplaceFileAtomically(const std::filesystem::path &temporary,
                           const std::filesystem::path &target) {
#if defined(_WIN32)
  return MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (::rename(temporary.c_str(), target.c_str()) != 0) {
    return false;
  }

  const std::filesystem::path parent = target.parent_path().empty()
                                           ? std::filesystem::path(".")
                                           : target.parent_path();
  int directory_flags = O_RDONLY;
#if defined(O_DIRECTORY)
  directory_flags |= O_DIRECTORY;
#endif
  const int directory = open(parent.c_str(), directory_flags);
  if (directory >= 0) {
    const bool synced = fsync(directory) == 0;
    close(directory);
    return synced;
  }
  return false;
#endif
}

bool WriteFileAtomically(const std::filesystem::path &target,
                         const void *data, size_t size) {
  std::error_code ec;
  const std::filesystem::path parent = target.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return false;
    }
  }

  const std::filesystem::path temporary = TemporaryPathFor(target);
#if defined(_WIN32)
  FILE *file = _wfopen(temporary.c_str(), L"wb");
#else
  FILE *file = std::fopen(temporary.c_str(), "wb");
#endif
  if (!file) {
    return false;
  }

  const bool written = std::fwrite(data, 1, size, file) == size;
  const bool flushed = written && FlushFileToDisk(file);
  const bool closed = std::fclose(file) == 0;
  if (!written || !flushed || !closed ||
      !ReplaceFileAtomically(temporary, target)) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
  return true;
}

} // namespace

SettingsManager::SettingsManager(GuiRuntime &owner) : owner_(owner) {}

int SettingsManager::Save() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  return SaveLocked();
}

int SettingsManager::SaveLocked() {
  CopyString(cache_v2_.client_id_with_password,
             owner_.client_id_with_password_);
  std::memcpy(cache_v2_.key, owner_.aes128_key_, sizeof(owner_.aes128_key_));
  std::memcpy(cache_v2_.iv, owner_.aes128_iv_, sizeof(owner_.aes128_iv_));
  CopyString(cache_v2_.self_hosted_id, owner_.self_hosted_id_);

  cache_v3_.magic = kCacheV3Magic;
  cache_v3_.version = kCacheV3Version;
  cache_v3_.base = cache_v2_;
  cache_v3_.pending_identity[sizeof(cache_v3_.pending_identity) - 1] = '\0';
  cache_v3_.pending_server_host[sizeof(cache_v3_.pending_server_host) - 1] =
      '\0';
  cache_v3_.pending_request_id[sizeof(cache_v3_.pending_request_id) - 1] =
      '\0';
  cache_v3_.checksum =
      CacheChecksum(&cache_v3_, offsetof(CacheV3, checksum));

  if (!WriteFileAtomically(owner_.cache_path_ + "/secure_cache_v3.enc",
                           &cache_v3_, sizeof(cache_v3_))) {
    return -1;
  }

  if (!WriteFileAtomically(owner_.cache_path_ + "/secure_cache_v2.enc",
                           &cache_v2_, sizeof(cache_v2_))) {
    LOG_WARN("Failed to update legacy v2 credential cache");
  }

  // Keep writing the legacy cache while older installations may still read it.
  CopyString(cache_v1_.client_id_with_password,
             owner_.client_id_with_password_);
  std::memcpy(cache_v1_.key, owner_.aes128_key_, sizeof(owner_.aes128_key_));
  std::memcpy(cache_v1_.iv, owner_.aes128_iv_, sizeof(owner_.aes128_iv_));
  if (!WriteFileAtomically(owner_.cache_path_ + "/secure_cache.enc",
                           &cache_v1_, sizeof(cache_v1_))) {
    LOG_WARN("Failed to update legacy v1 credential cache");
  }

  return 0;
}

bool SettingsManager::ReadV3Locked() {
  std::ifstream cache_file(owner_.cache_path_ + "/secure_cache_v3.enc",
                           std::ios::binary);
  if (!cache_file) {
    return false;
  }

  CacheV3 loaded{};
  cache_file.read(reinterpret_cast<char *>(&loaded), sizeof(loaded));
  if (cache_file.gcount() != static_cast<std::streamsize>(sizeof(loaded)) ||
      loaded.magic != kCacheV3Magic ||
      loaded.version != kCacheV3Version ||
      loaded.checksum != CacheChecksum(&loaded, offsetof(CacheV3, checksum))) {
    LOG_WARN("Ignore invalid v3 credential cache");
    return false;
  }

  loaded.base.client_id_with_password
      [sizeof(loaded.base.client_id_with_password) - 1] = '\0';
  loaded.base.self_hosted_id[sizeof(loaded.base.self_hosted_id) - 1] = '\0';
  loaded.pending_identity[sizeof(loaded.pending_identity) - 1] = '\0';
  loaded.pending_server_host[sizeof(loaded.pending_server_host) - 1] = '\0';
  loaded.pending_request_id[sizeof(loaded.pending_request_id) - 1] = '\0';
  cache_v3_ = loaded;
  cache_v2_ = loaded.base;
  return true;
}

bool SettingsManager::ReadV2Locked() {
  std::ifstream cache_v2_file(owner_.cache_path_ + "/secure_cache_v2.enc",
                              std::ios::binary);
  if (!cache_v2_file) {
    return false;
  }
  cache_v2_file.read(reinterpret_cast<char *>(&cache_v2_), sizeof(cache_v2_));
  if (cache_v2_file.gcount() !=
      static_cast<std::streamsize>(sizeof(cache_v2_))) {
    return false;
  }
  cache_v2_
      .client_id_with_password[sizeof(cache_v2_.client_id_with_password) - 1] =
      '\0';
  cache_v2_.self_hosted_id[sizeof(cache_v2_.self_hosted_id) - 1] = '\0';
  return true;
}

int SettingsManager::Load() {
  std::unique_lock<std::mutex> lock(cache_mutex_);

  const bool loaded_v3 = ReadV3Locked();
  if (loaded_v3 || ReadV2Locked()) {
    CopyString(owner_.client_id_with_password_,
               cache_v2_.client_id_with_password);
    CopyString(owner_.self_hosted_id_, cache_v2_.self_hosted_id);
    std::memcpy(owner_.aes128_key_, cache_v2_.key, sizeof(cache_v2_.key));
    std::memcpy(owner_.aes128_iv_, cache_v2_.iv, sizeof(cache_v2_.iv));
    if (loaded_v3) {
      LOG_INFO("Load settings from v3 cache file");
    } else {
      cache_v3_ = {};
      SaveLocked();
      LOG_INFO("Migrated settings from v2 to v3 cache file");
    }
  } else {
    std::ifstream cache_v1_file(owner_.cache_path_ + "/secure_cache.enc",
                                std::ios::binary);
    if (!cache_v1_file) {
      lock.unlock();

      std::memset(owner_.password_saved_, 0, sizeof(owner_.password_saved_));
      std::memset(owner_.aes128_key_, 0, sizeof(owner_.aes128_key_));
      std::memset(owner_.aes128_iv_, 0, sizeof(owner_.aes128_iv_));
      std::memset(owner_.self_hosted_id_, 0, sizeof(owner_.self_hosted_id_));

      owner_.thumbnail_ =
          std::make_shared<Thumbnail>(owner_.cache_path_ + "/thumbnails/");
      owner_.thumbnail_->GetKeyAndIv(owner_.aes128_key_, owner_.aes128_iv_);
      owner_.thumbnail_->DeleteAllFilesInDirectory();

      Save();
      return -1;
    }

    cache_v1_file.read(reinterpret_cast<char *>(&cache_v1_), sizeof(cache_v1_));
    cache_v1_
        .client_id_with_password[sizeof(cache_v1_.client_id_with_password) -
                                 1] = '\0';

    CopyString(cache_v2_.client_id_with_password,
               cache_v1_.client_id_with_password);
    std::memcpy(cache_v2_.key, cache_v1_.key, sizeof(cache_v1_.key));
    std::memcpy(cache_v2_.iv, cache_v1_.iv, sizeof(cache_v1_.iv));
    std::memset(cache_v2_.self_hosted_id, 0, sizeof(cache_v2_.self_hosted_id));

    CopyString(owner_.client_id_with_password_,
               cache_v1_.client_id_with_password);
    std::memset(owner_.self_hosted_id_, 0, sizeof(owner_.self_hosted_id_));
    std::memcpy(owner_.aes128_key_, cache_v1_.key, sizeof(cache_v1_.key));
    std::memcpy(owner_.aes128_iv_, cache_v1_.iv, sizeof(cache_v1_.iv));

    cache_v3_ = {};
    SaveLocked();
    LOG_INFO("Migrated settings from v1 to v3 cache file");
  }

  lock.unlock();

  ActivateCachedPublicIdentity();

  owner_.thumbnail_ =
      std::make_shared<Thumbnail>(owner_.cache_path_ + "/thumbnails/",
                                  owner_.aes128_key_, owner_.aes128_iv_);

  owner_.language_button_value_ = localization::detail::ClampLanguageIndex(
      static_cast<int>(owner_.config_center_->GetLanguage()));
  owner_.video_quality_button_value_ =
      static_cast<int>(owner_.config_center_->GetVideoQuality());
  owner_.video_frame_rate_button_value_ =
      static_cast<int>(owner_.config_center_->GetVideoFrameRate());
  owner_.video_adaptation_policy_button_value_ =
      static_cast<int>(owner_.config_center_->GetVideoAdaptationPolicy());
  owner_.video_encode_format_button_value_ =
      static_cast<int>(owner_.config_center_->GetVideoEncodeFormat());
  owner_.enable_hardware_video_codec_ =
      owner_.config_center_->IsHardwareVideoCodec();
  owner_.enable_turn_ = owner_.config_center_->IsEnableTurn();
  owner_.enable_srtp_ = owner_.config_center_->IsEnableSrtp();
  owner_.enable_self_hosted_ = owner_.config_center_->IsSelfHosted();
  owner_.enable_autostart_ = owner_.config_center_->IsEnableAutostart();
  owner_.enable_daemon_ = owner_.config_center_->IsEnableDaemon();
#if _WIN32 && CROSSDESK_PORTABLE
  owner_.portable_service_prompt_suppressed_ =
      owner_.config_center_->IsPortableServicePromptSuppressed();
  owner_.portable_service_do_not_remind_ =
      owner_.portable_service_prompt_suppressed_;
#endif

  const std::string saved_path =
      owner_.config_center_->GetFileTransferSavePath();
  CopyString(owner_.file_transfer_save_path_buf_, saved_path.c_str());
  owner_.file_transfer_save_path_last_ = saved_path;

  owner_.language_button_value_last_ = owner_.language_button_value_;
  owner_.video_quality_button_value_last_ = owner_.video_quality_button_value_;
  owner_.video_frame_rate_button_value_last_ =
      owner_.video_frame_rate_button_value_;
  owner_.video_adaptation_policy_button_value_last_ =
      owner_.video_adaptation_policy_button_value_;
  owner_.video_encode_format_button_value_last_ =
      owner_.video_encode_format_button_value_;
  owner_.enable_hardware_video_codec_last_ =
      owner_.enable_hardware_video_codec_;
  owner_.enable_turn_last_ = owner_.enable_turn_;
  owner_.enable_srtp_last_ = owner_.enable_srtp_;
  owner_.enable_self_hosted_last_ = owner_.enable_self_hosted_;
  owner_.enable_autostart_last_ = owner_.enable_autostart_;

  LOG_INFO("Load settings from cache file");
  return 0;
}

bool SettingsManager::LoadCachedSelfHostedIdentity() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  if ((!ReadV3Locked() && !ReadV2Locked()) ||
      cache_v2_.self_hosted_id[0] == '\0') {
    std::memset(owner_.self_hosted_id_, 0, sizeof(owner_.self_hosted_id_));
    std::memset(owner_.client_id_, 0, sizeof(owner_.client_id_));
    std::memset(owner_.password_saved_, 0, sizeof(owner_.password_saved_));
    return false;
  }

  CopyString(owner_.self_hosted_id_, cache_v2_.self_hosted_id);
  const char *at_pos = std::strchr(owner_.self_hosted_id_, '@');
  if (at_pos == nullptr) {
    CopyString(owner_.client_id_, owner_.self_hosted_id_);
    std::memset(owner_.password_saved_, 0, sizeof(owner_.password_saved_));
  } else {
    const std::string id(owner_.self_hosted_id_,
                         at_pos - owner_.self_hosted_id_);
    CopyString(owner_.client_id_, id.c_str());
    CopyString(owner_.password_saved_, at_pos + 1);
  }
  return true;
}

bool SettingsManager::ActivateCachedPublicIdentity() {
  if (owner_.client_id_with_password_[0] == '\0') {
    std::memset(owner_.client_id_, 0, sizeof(owner_.client_id_));
    std::memset(owner_.password_saved_, 0, sizeof(owner_.password_saved_));
    return false;
  }

  const char *at_pos = std::strchr(owner_.client_id_with_password_, '@');
  if (at_pos == nullptr) {
    CopyString(owner_.client_id_, owner_.client_id_with_password_);
    std::memset(owner_.password_saved_, 0, sizeof(owner_.password_saved_));
  } else {
    const std::string id(owner_.client_id_with_password_,
                         at_pos - owner_.client_id_with_password_);
    CopyString(owner_.client_id_, id.c_str());
    CopyString(owner_.password_saved_, at_pos + 1);
  }
  return owner_.client_id_[0] != '\0';
}

void SettingsManager::ActivateIdentity(const char *identity,
                                       bool self_hosted) {
  if (!identity) {
    return;
  }

  if (self_hosted) {
    CopyString(owner_.self_hosted_id_, identity);
  } else {
    CopyString(owner_.client_id_with_password_, identity);
  }

  const char *at_pos = std::strchr(identity, '@');
  if (at_pos == nullptr) {
    CopyString(owner_.client_id_, identity);
    std::memset(owner_.password_saved_, 0, sizeof(owner_.password_saved_));
    return;
  }

  const std::string id(identity, at_pos - identity);
  CopyString(owner_.client_id_, id.c_str());
  CopyString(owner_.password_saved_, at_pos + 1);
}

bool SettingsManager::StagePendingPasswordChange(
    const std::string &identity, bool self_hosted,
    const std::string &server_host, int server_port,
    const std::string &request_id) {
  if (identity.empty() || identity.size() >= sizeof(cache_v3_.pending_identity) ||
      server_host.empty() ||
      server_host.size() >= sizeof(cache_v3_.pending_server_host) ||
      server_port <= 0 || request_id.empty() ||
      request_id.size() >= sizeof(cache_v3_.pending_request_id)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (cache_v3_.pending_identity[0] != '\0' &&
      (cache_v3_.pending_self_hosted != self_hosted ||
       cache_v3_.pending_server_port != server_port ||
       server_host != cache_v3_.pending_server_host ||
       request_id != cache_v3_.pending_request_id)) {
    LOG_WARN("Refuse to overwrite an unresolved password change");
    return false;
  }
  const CacheV3 previous = cache_v3_;
  CopyString(cache_v3_.pending_identity, identity.c_str());
  CopyString(cache_v3_.pending_server_host, server_host.c_str());
  cache_v3_.pending_server_port = server_port;
  cache_v3_.pending_self_hosted = self_hosted;
  CopyString(cache_v3_.pending_request_id, request_id.c_str());
  if (SaveLocked() != 0) {
    cache_v3_ = previous;
    return false;
  }
  return true;
}

std::string SettingsManager::PendingPasswordChangeIdentity(
    bool self_hosted, const std::string &server_host, int server_port) const {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (cache_v3_.pending_identity[0] == '\0' ||
      cache_v3_.pending_self_hosted != self_hosted ||
      cache_v3_.pending_server_port != server_port ||
      server_host != cache_v3_.pending_server_host) {
    return {};
  }
  return cache_v3_.pending_identity;
}

bool SettingsManager::PromotePendingPasswordChange() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (cache_v3_.pending_identity[0] == '\0') {
    return true;
  }

  const CacheV3 previous = cache_v3_;
  const std::string identity = cache_v3_.pending_identity;
  const bool self_hosted = cache_v3_.pending_self_hosted;
  ActivateIdentity(identity.c_str(), self_hosted);
  std::memset(cache_v3_.pending_identity, 0,
              sizeof(cache_v3_.pending_identity));
  std::memset(cache_v3_.pending_server_host, 0,
              sizeof(cache_v3_.pending_server_host));
  cache_v3_.pending_server_port = 0;
  cache_v3_.pending_self_hosted = false;
  std::memset(cache_v3_.pending_request_id, 0,
              sizeof(cache_v3_.pending_request_id));
  if (SaveLocked() != 0) {
    // The already-durable pending credential remains the recovery source on
    // disk. Keep it in memory as well so a reconnect in this process retries
    // the credential that the server has accepted.
    cache_v3_ = previous;
    return false;
  }
  return true;
}

bool SettingsManager::ClearPendingPasswordChange() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (cache_v3_.pending_identity[0] == '\0') {
    return true;
  }

  const CacheV3 previous = cache_v3_;
  std::memset(cache_v3_.pending_identity, 0,
              sizeof(cache_v3_.pending_identity));
  std::memset(cache_v3_.pending_server_host, 0,
              sizeof(cache_v3_.pending_server_host));
  cache_v3_.pending_server_port = 0;
  cache_v3_.pending_self_hosted = false;
  std::memset(cache_v3_.pending_request_id, 0,
              sizeof(cache_v3_.pending_request_id));
  if (SaveLocked() != 0) {
    cache_v3_ = previous;
    return false;
  }
  return true;
}

void SettingsManager::PersistSelfHostedIdentity(const char *client_id) {
  if (!client_id) {
    return;
  }

  std::lock_guard<std::mutex> lock(cache_mutex_);
  CopyString(owner_.self_hosted_id_, client_id);
  if (SaveLocked() != 0) {
    LOG_ERROR("Failed to persist self-hosted identity atomically");
  }
}

int SettingsManager::LoadRecentConnectionAliases() {
  recent_connection_aliases_.clear();

  std::ifstream alias_file(owner_.cache_path_ +
                           "/recent_connection_aliases.json");
  if (!alias_file.good()) {
    return 0;
  }

  try {
    nlohmann::json alias_json;
    alias_file >> alias_json;

    const nlohmann::json *aliases = &alias_json;
    if (alias_json.contains("aliases") && alias_json["aliases"].is_object()) {
      aliases = &alias_json["aliases"];
    }
    if (!aliases->is_object()) {
      LOG_WARN("Invalid recent connection alias file");
      return -1;
    }

    for (auto it = aliases->begin(); it != aliases->end(); ++it) {
      if (it.value().is_string()) {
        const std::string remote_id = it.key();
        const std::string alias = it.value().get<std::string>();
        if (!remote_id.empty() && !alias.empty()) {
          recent_connection_aliases_[remote_id] = alias;
        }
      }
    }
  } catch (const std::exception &e) {
    LOG_WARN("Load recent connection aliases failed: {}", e.what());
    return -1;
  }
  return 0;
}

int SettingsManager::SaveRecentConnectionAliases() const {
  std::error_code ec;
  std::filesystem::create_directories(owner_.cache_path_, ec);
  if (ec) {
    LOG_WARN("Create cache directory failed while saving aliases: {}",
             ec.message());
    return -1;
  }

  nlohmann::json alias_json;
  alias_json["aliases"] = nlohmann::json::object();
  for (const auto &[remote_id, alias] : recent_connection_aliases_) {
    if (!remote_id.empty() && !alias.empty()) {
      alias_json["aliases"][remote_id] = alias;
    }
  }

  std::ofstream alias_file(
      owner_.cache_path_ + "/recent_connection_aliases.json", std::ios::trunc);
  if (!alias_file.good()) {
    LOG_WARN("Open recent connection alias file failed");
    return -1;
  }
  alias_file << alias_json.dump(2);
  return 0;
}

std::string SettingsManager::RecentConnectionDisplayName(
    const Thumbnail::RecentConnection &connection) const {
  const auto alias_it = recent_connection_aliases_.find(connection.remote_id);
  if (alias_it != recent_connection_aliases_.end() &&
      !alias_it->second.empty()) {
    return alias_it->second;
  }
  if (!connection.remote_host_name.empty() &&
      connection.remote_host_name != "unknown") {
    return connection.remote_host_name;
  }
  return connection.remote_id;
}

void SettingsManager::BeginEditRecentConnectionAlias(
    const Thumbnail::RecentConnection &connection) {
  owner_.edit_connection_alias_remote_id_ = connection.remote_id;
  std::memset(owner_.edit_connection_alias_, 0,
              sizeof(owner_.edit_connection_alias_));

  const auto alias_it = recent_connection_aliases_.find(connection.remote_id);
  const std::string alias = alias_it != recent_connection_aliases_.end()
                                ? alias_it->second
                                : RecentConnectionDisplayName(connection);
  CopyString(owner_.edit_connection_alias_, alias.c_str());

  owner_.focus_on_input_widget_ = true;
  owner_.show_edit_connection_alias_window_ = true;
}

void SettingsManager::SetRecentConnectionAlias(const std::string &remote_id,
                                               const std::string &alias) {
  if (!remote_id.empty()) {
    recent_connection_aliases_[remote_id] = alias;
  }
}

void SettingsManager::EraseRecentConnectionAlias(const std::string &remote_id) {
  recent_connection_aliases_.erase(remote_id);
}

} // namespace crossdesk
