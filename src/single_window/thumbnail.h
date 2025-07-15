/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-07
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _THUMBNAIL_H_
#define _THUMBNAIL_H_

#include <SDL.h>

#include <filesystem>
#include <map>
#include <unordered_map>
#include <vector>

class Thumbnail {
 public:
  struct RecentConnection {
    SDL_Texture* texture = nullptr;
    std::string remote_id;
    std::string remote_host_name;
    std::string password;
    bool remember_password = false;
  };

 public:
  Thumbnail();
  explicit Thumbnail(unsigned char* aes128_key, unsigned char* aes128_iv);
  ~Thumbnail();

 public:
  int SaveToThumbnail(const char* yuv420p, int width, int height,
                      const std::string& remote_id,
                      const std::string& host_name,
                      const std::string& password);

  int LoadThumbnail(
      SDL_Renderer* renderer,
      std::unordered_map<std::string, RecentConnection>& recent_connections,
      int* width, int* height);

  int DeleteThumbnail(const std::string& filename_keyword);

  int DeleteAllFilesInDirectory();

  int GetKey(unsigned char* aes128_key) {
    memcpy(aes128_key, aes128_key_, sizeof(aes128_key_));
    return sizeof(aes128_key_);
  }

  int GetIv(unsigned char* aes128_iv) {
    memcpy(aes128_iv, aes128_iv_, sizeof(aes128_iv_));
    return sizeof(aes128_iv_);
  }

  int GetKeyAndIv(unsigned char* aes128_key, unsigned char* aes128_iv) {
    memcpy(aes128_key, aes128_key_, sizeof(aes128_key_));
    memcpy(aes128_iv, aes128_iv_, sizeof(aes128_iv_));
    return 0;
  }

 private:
  std::vector<std::filesystem::path> FindThumbnailPath(
      const std::filesystem::path& directory);

  std::string AES_encrypt(const std::string& plaintext, unsigned char* key,
                          unsigned char* iv);

  std::string AES_decrypt(const std::string& ciphertext, unsigned char* key,
                          unsigned char* iv);

 private:
  int thumbnail_width_ = 160;
  int thumbnail_height_ = 90;
  char* rgba_buffer_ = nullptr;
  std::string image_path_ = "thumbnails/";

  unsigned char aes128_key_[16];
  unsigned char aes128_iv_[16];
  unsigned char ciphertext_[64];
  unsigned char decryptedtext_[64];
};

#endif