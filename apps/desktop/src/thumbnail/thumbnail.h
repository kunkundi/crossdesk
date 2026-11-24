/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _THUMBNAIL_H_
#define _THUMBNAIL_H_

#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "remote_action.h"

namespace crossdesk {

class Thumbnail {
 public:
  struct LanTarget {
    std::string address, device_id;
    unsigned port = 52888;
  };
  static std::string LanRecordId(const std::string& device_id);
  struct RecentConnection {
    std::filesystem::path image_path;
    std::string remote_id;
    std::string remote_host_name;
    std::string password;
    bool remember_password = false;
    LanTarget lan;
    HostPlatform platform = HostPlatform::Unknown;
  };

 public:
  Thumbnail(std::string save_path);
  explicit Thumbnail(std::string save_path, unsigned char* aes128_key,
                     unsigned char* aes128_iv);

  // Serialized with writes so disabling also clears any in-flight preview.
  int SetSavePreviews(bool enabled);

  // Also records metadata without an image. Empty password means do not store
  // credentials (and clears any remembered password from a previous session).
  int SaveToThumbnail(const char* nv12, int width, int height,
                      const std::string& remote_id,
                      const std::string& host_name, const std::string& password,
                      HostPlatform platform = HostPlatform::Unknown,
                      const LanTarget* lan = nullptr,
                      std::string_view wallpaper = {});

  int LoadThumbnail(
      std::vector<std::pair<std::string, Thumbnail::RecentConnection>>&
          recent_connections);

  // Crop from the saved master and downsample for a physical-pixel viewport.
  // Low-resolution legacy images are never enlarged in the cache.
  bool DecodeImage(const std::filesystem::path& image_path,
                   int target_width, int target_height,
                   std::vector<unsigned char>* rgba, int* width,
                   int* height) const;

  int DeleteThumbnail(const std::string& filename_keyword);

  int DeleteAllFilesInDirectory();

  int GetKey(unsigned char* aes128_key) {
    std::memcpy(aes128_key, aes128_key_, sizeof(aes128_key_));
    return sizeof(aes128_key_);
  }

  int GetIv(unsigned char* aes128_iv) {
    std::memcpy(aes128_iv, aes128_iv_, sizeof(aes128_iv_));
    return sizeof(aes128_iv_);
  }

  int GetKeyAndIv(unsigned char* aes128_key, unsigned char* aes128_iv) {
    std::memcpy(aes128_key, aes128_key_, sizeof(aes128_key_));
    std::memcpy(aes128_iv, aes128_iv_, sizeof(aes128_iv_));
    return 0;
  }

 private:
  int LoadThumbnailLocked(
      std::vector<std::pair<std::string, RecentConnection>>& connections);
  bool WriteRecord(const RecentConnection& connection);
  bool ReadRecord(const std::filesystem::path& path, RecentConnection* connection);

  std::vector<std::filesystem::path> FindThumbnailPath(
      const std::filesystem::path& directory);

  std::string AES_encrypt(const std::string& plaintext, unsigned char* key,
                          unsigned char* iv);

  std::string AES_decrypt(const std::string& ciphertext, unsigned char* key,
                          unsigned char* iv);

 private:
  mutable std::mutex mutex_;
  bool save_previews_ = true;
  std::string save_path_ = "thumbnails/";

  unsigned char aes128_key_[16];
  unsigned char aes128_iv_[16];
  unsigned char ciphertext_[64];
  unsigned char decryptedtext_[64];
};
}  // namespace crossdesk
#endif
