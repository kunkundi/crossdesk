#include "features/settings/settings_manager.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "localization.h"
#include "rd_log.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {
namespace {

template <size_t Size>
void CopyString(char (&destination)[Size], const char *source) {
  static_assert(Size > 0);
  std::memset(destination, 0, Size);
  if (source) {
    std::strncpy(destination, source, Size - 1);
  }
}

} // namespace

SettingsManager::SettingsManager(GuiRuntime &owner) : owner_(owner) {}

int SettingsManager::Save() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  return SaveLocked();
}

int SettingsManager::SaveLocked() {
  std::ofstream cache_v2_file(owner_.cache_path_ + "/secure_cache_v2.enc",
                              std::ios::binary);
  if (!cache_v2_file) {
    return -1;
  }

  CopyString(cache_v2_.client_id_with_password,
             owner_.client_id_with_password_);
  std::memcpy(cache_v2_.key, owner_.aes128_key_, sizeof(owner_.aes128_key_));
  std::memcpy(cache_v2_.iv, owner_.aes128_iv_, sizeof(owner_.aes128_iv_));
  CopyString(cache_v2_.self_hosted_id, owner_.self_hosted_id_);

  cache_v2_file.write(reinterpret_cast<const char *>(&cache_v2_),
                      sizeof(cache_v2_));

  // Keep writing the legacy cache while older installations may still read it.
  std::ofstream cache_v1_file(owner_.cache_path_ + "/secure_cache.enc",
                              std::ios::binary);
  if (cache_v1_file) {
    CopyString(cache_v1_.client_id_with_password,
               owner_.client_id_with_password_);
    std::memcpy(cache_v1_.key, owner_.aes128_key_, sizeof(owner_.aes128_key_));
    std::memcpy(cache_v1_.iv, owner_.aes128_iv_, sizeof(owner_.aes128_iv_));
    cache_v1_file.write(reinterpret_cast<const char *>(&cache_v1_),
                        sizeof(cache_v1_));
  }

  return 0;
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

  if (ReadV2Locked()) {
    CopyString(owner_.client_id_with_password_,
               cache_v2_.client_id_with_password);
    CopyString(owner_.self_hosted_id_, cache_v2_.self_hosted_id);
    std::memcpy(owner_.aes128_key_, cache_v2_.key, sizeof(cache_v2_.key));
    std::memcpy(owner_.aes128_iv_, cache_v2_.iv, sizeof(cache_v2_.iv));
    LOG_INFO("Load settings from v2 cache file");
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

    SaveLocked();
    LOG_INFO("Migrated settings from v1 to v2 cache file");
  }

  lock.unlock();

  const char *at_pos = std::strchr(owner_.client_id_with_password_, '@');
  if (at_pos != nullptr) {
    const std::string id(owner_.client_id_with_password_,
                         at_pos - owner_.client_id_with_password_);
    const std::string password(at_pos + 1);
    CopyString(owner_.client_id_, id.c_str());
    CopyString(owner_.password_saved_, password.c_str());
  }

  owner_.thumbnail_ =
      std::make_shared<Thumbnail>(owner_.cache_path_ + "/thumbnails/",
                                  owner_.aes128_key_, owner_.aes128_iv_);

  owner_.language_button_value_ = localization::detail::ClampLanguageIndex(
      static_cast<int>(owner_.config_center_->GetLanguage()));
  owner_.video_quality_button_value_ =
      static_cast<int>(owner_.config_center_->GetVideoQuality());
  owner_.video_frame_rate_button_value_ =
      static_cast<int>(owner_.config_center_->GetVideoFrameRate());
  owner_.video_encode_format_button_value_ =
      static_cast<int>(owner_.config_center_->GetVideoEncodeFormat());
  owner_.enable_hardware_video_codec_ =
      owner_.config_center_->IsHardwareVideoCodec();
  owner_.enable_turn_ = owner_.config_center_->IsEnableTurn();
  owner_.enable_srtp_ = owner_.config_center_->IsEnableSrtp();
  owner_.enable_self_hosted_ = owner_.config_center_->IsSelfHosted();
  owner_.enable_autostart_ = owner_.config_center_->IsEnableAutostart();
  owner_.enable_daemon_ = owner_.config_center_->IsEnableDaemon();
  owner_.enable_minimize_to_tray_ = owner_.config_center_->IsMinimizeToTray();
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
  owner_.video_encode_format_button_value_last_ =
      owner_.video_encode_format_button_value_;
  owner_.enable_hardware_video_codec_last_ =
      owner_.enable_hardware_video_codec_;
  owner_.enable_turn_last_ = owner_.enable_turn_;
  owner_.enable_srtp_last_ = owner_.enable_srtp_;
  owner_.enable_self_hosted_last_ = owner_.enable_self_hosted_;
  owner_.enable_autostart_last_ = owner_.enable_autostart_;
  owner_.enable_minimize_to_tray_last_ = owner_.enable_minimize_to_tray_;

  LOG_INFO("Load settings from cache file");
  return 0;
}

bool SettingsManager::LoadCachedSelfHostedIdentity() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (!ReadV2Locked() || cache_v2_.self_hosted_id[0] == '\0') {
    std::memset(owner_.self_hosted_id_, 0, sizeof(owner_.self_hosted_id_));
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

void SettingsManager::PersistSelfHostedIdentity(const char *client_id) {
  if (!client_id) {
    return;
  }

  std::lock_guard<std::mutex> lock(cache_mutex_);
  if (!ReadV2Locked()) {
    cache_v2_ = {};
  }

  CopyString(cache_v2_.self_hosted_id, client_id);
  CopyString(cache_v2_.client_id_with_password,
             owner_.client_id_with_password_);
  std::memcpy(cache_v2_.key, owner_.aes128_key_, sizeof(owner_.aes128_key_));
  std::memcpy(cache_v2_.iv, owner_.aes128_iv_, sizeof(owner_.aes128_iv_));

  std::ofstream cache_v2_file(owner_.cache_path_ + "/secure_cache_v2.enc",
                              std::ios::binary);
  if (cache_v2_file) {
    cache_v2_file.write(reinterpret_cast<const char *>(&cache_v2_),
                        sizeof(cache_v2_));
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
