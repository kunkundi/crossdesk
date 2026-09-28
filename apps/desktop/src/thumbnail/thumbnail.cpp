#include "thumbnail.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "libyuv.h"
#include "rd_log.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace crossdesk {

void ScaleNv12ToABGR(char* src, int src_w, int src_h, int dst_w, int dst_h,
                     char* dst_rgba) {
  uint8_t* y = reinterpret_cast<uint8_t*>(src);
  uint8_t* uv = y + src_w * src_h;

  float src_aspect = float(src_w) / src_h;
  float dst_aspect = float(dst_w) / dst_h;
  int fit_w = dst_w, fit_h = dst_h;
  if (src_aspect > dst_aspect) {
    fit_h = int(dst_w / src_aspect);
  } else {
    fit_w = int(dst_h * src_aspect);
  }

  std::vector<uint8_t> y_i420(src_w * src_h);
  std::vector<uint8_t> u_i420((src_w / 2) * (src_h / 2));
  std::vector<uint8_t> v_i420((src_w / 2) * (src_h / 2));
  libyuv::NV12ToI420(y, src_w, uv, src_w, y_i420.data(), src_w, u_i420.data(),
                     src_w / 2, v_i420.data(), src_w / 2, src_w, src_h);

  std::vector<uint8_t> y_fit(fit_w * fit_h);
  std::vector<uint8_t> u_fit((fit_w + 1) / 2 * (fit_h + 1) / 2);
  std::vector<uint8_t> v_fit((fit_w + 1) / 2 * (fit_h + 1) / 2);
  libyuv::I420Scale(y_i420.data(), src_w, u_i420.data(), src_w / 2,
                    v_i420.data(), src_w / 2, src_w, src_h, y_fit.data(), fit_w,
                    u_fit.data(), (fit_w + 1) / 2, v_fit.data(),
                    (fit_w + 1) / 2, fit_w, fit_h, libyuv::kFilterBilinear);

  std::vector<uint8_t> abgr(fit_w * fit_h * 4);
  libyuv::I420ToABGR(y_fit.data(), fit_w, u_fit.data(), (fit_w + 1) / 2,
                     v_fit.data(), (fit_w + 1) / 2, abgr.data(), fit_w * 4,
                     fit_w, fit_h);

  std::memset(dst_rgba, 0, dst_w * dst_h * 4);
  for (int i = 0; i < dst_w * dst_h; ++i) {
    dst_rgba[i * 4 + 3] = static_cast<char>(0xFF);
  }

  for (int row = 0; row < fit_h; ++row) {
    int dst_offset =
        ((row + (dst_h - fit_h) / 2) * dst_w + (dst_w - fit_w) / 2) * 4;
    std::memcpy(dst_rgba + dst_offset, abgr.data() + row * fit_w * 4,
                fit_w * 4);
  }
}

Thumbnail::Thumbnail(std::string save_path) {
  if (!save_path.empty()) {
    save_path_ = save_path;
  }

  RAND_bytes(aes128_key_, sizeof(aes128_key_));
  RAND_bytes(aes128_iv_, sizeof(aes128_iv_));
  std::filesystem::create_directories(save_path_);
}

Thumbnail::Thumbnail(std::string save_path, unsigned char* aes128_key,
                     unsigned char* aes128_iv) {
  if (!save_path.empty()) {
    save_path_ = save_path;
  }

  std::memcpy(aes128_key_, aes128_key, sizeof(aes128_key_));
  std::memcpy(aes128_iv_, aes128_iv, sizeof(aes128_iv_));
  std::filesystem::create_directories(save_path_);
}

Thumbnail::~Thumbnail() {
  if (rgba_buffer_) {
    delete[] rgba_buffer_;
    rgba_buffer_ = nullptr;
  }
}

int Thumbnail::SetThumbnailDpiScale(float dpi_scale) {
  thumbnail_width_ = static_cast<int>(thumbnail_width_ * dpi_scale);
  thumbnail_height_ = static_cast<int>(thumbnail_height_ * dpi_scale);
  return 0;
}

namespace {

bool IsRemoteId(const std::string& id) {
  return id.size() == 9 &&
         std::all_of(id.begin(), id.end(),
                     [](unsigned char c) { return c >= '0' && c <= '9'; });
}

std::string ConnectionKey(const Thumbnail::RecentConnection& connection) {
  return connection.remote_id + (connection.remember_password ? "Y" : "N") +
         connection.remote_host_name +
         (connection.remember_password ? "@" + connection.password : "");
}

}  // namespace

bool Thumbnail::WriteRecord(const RecentConnection& connection) {
  const auto path =
      std::filesystem::path(save_path_) / (connection.remote_id + ".json");
  const auto temporary = path.string() + ".tmp";
  const nlohmann::json record = {
      {"remote_id", connection.remote_id},
      {"host_name", connection.remote_host_name},
      {"password",
       connection.remember_password
           ? AES_encrypt(connection.password, aes128_key_, aes128_iv_)
           : ""},
      {"platform", HostPlatformName(connection.platform)}};
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output << record.dump();
  output.close();
  if (!output) return false;
  std::error_code error;
#ifdef _WIN32
  // Windows rename does not replace an existing destination.
  if (!MoveFileExW(std::filesystem::path(temporary).c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary, error);
    return false;
  }
#else
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return false;
  }
#endif
  return true;
}

bool Thumbnail::ReadRecord(const std::filesystem::path& path,
                           RecentConnection* connection) {
  std::ifstream input(path, std::ios::binary);
  const auto record = nlohmann::json::parse(input, nullptr, false);
  if (!record.is_object()) return false;
  try {
    connection->remote_id = record.value("remote_id", "");
    if (!IsRemoteId(connection->remote_id) ||
        path.stem() != connection->remote_id)
      return false;
    connection->remote_host_name = record.value("host_name", "");
    const auto password = record.value("password", "");
    connection->remember_password = !password.empty();
    connection->password =
        password.empty() ? "" : AES_decrypt(password, aes128_key_, aes128_iv_);
    connection->platform =
        ParseHostPlatform(record.value("platform", "unknown"));
    const auto image = path.parent_path() / (connection->remote_id + ".png");
    if (save_previews_ && std::filesystem::is_regular_file(image)) {
      connection->image_path = image;
    }
    return true;
  } catch (const nlohmann::json::exception&) {
    return false;
  }
}

int Thumbnail::SetSavePreviews(bool enabled) {
  std::lock_guard lock(mutex_);
  save_previews_ = enabled;
  if (enabled) return 0;
  // Migrate legacy filenames before removing their image contents, preserving
  // credentials, names and recency even when previews have never been enabled.
  std::vector<std::pair<std::string, RecentConnection>> connections;
  return LoadThumbnailLocked(connections);
}

int Thumbnail::SaveToThumbnail(const char* nv12, int width, int height,
                               const std::string& remote_id,
                               const std::string& host_name,
                               const std::string& password,
                               HostPlatform platform) {
  std::lock_guard lock(mutex_);
  // Preserve the existing opt-in via Remember password for recent connections.
  if (password.empty()) return 0;
  if (!IsRemoteId(remote_id)) return -1;
  RecentConnection connection;
  ReadRecord(std::filesystem::path(save_path_) / (remote_id + ".json"),
             &connection);
  connection.remote_id = remote_id;
  connection.remote_host_name = host_name;
  connection.password = password;
  connection.remember_password = true;
  if (platform != HostPlatform::Unknown) connection.platform = platform;
  if (!WriteRecord(connection)) return -1;

  const auto image = std::filesystem::path(save_path_) / (remote_id + ".png");
  if (save_previews_ && nv12 && width > 0 && height > 0) {
    if (!rgba_buffer_) {
      rgba_buffer_ = new char[thumbnail_width_ * thumbnail_height_ * 4];
    }
    ScaleNv12ToABGR(const_cast<char*>(nv12), width, height, thumbnail_width_,
                    thumbnail_height_, rgba_buffer_);
    if (!stbi_write_png(image.string().c_str(), thumbnail_width_,
                        thumbnail_height_, 4, rgba_buffer_,
                        thumbnail_width_ * 4))
      return -1;
  } else if (!save_previews_) {
    std::error_code error;
    std::filesystem::remove(image, error);
    if (error) return -1;
  }
  return 0;
}

int Thumbnail::LoadThumbnail(
    std::vector<std::pair<std::string, RecentConnection>>& recent_connections,
    int* width, int* height) {
  std::lock_guard lock(mutex_);
  if (width) *width = thumbnail_width_;
  if (height) *height = thumbnail_height_;
  return LoadThumbnailLocked(recent_connections);
}

int Thumbnail::LoadThumbnailLocked(
    std::vector<std::pair<std::string, RecentConnection>>& recent_connections) {
  recent_connections.clear();
  int result = 0;
  std::map<std::string, RecentConnection> records;
  const auto paths = FindThumbnailPath(save_path_);
  for (const auto& path : paths) {
    if (path.extension() != ".json") continue;
    RecentConnection connection;
    if (ReadRecord(path, &connection))
      records.emplace(connection.remote_id, connection);
  }
  for (const auto& path : paths) {
    const auto filename = path.filename().string();
    if (filename.size() < 10 || !IsRemoteId(filename.substr(0, 9)) ||
        (filename[9] != 'Y' && filename[9] != 'N'))
      continue;
    const auto remote_id = filename.substr(0, 9);
    if (!records.count(remote_id)) {
      RecentConnection connection;
      connection.remote_id = remote_id;
      connection.remember_password = filename[9] == 'Y';
      const auto separator = filename.find('@', 10);
      if (connection.remember_password && separator == std::string::npos)
        continue;
      connection.remote_host_name =
          filename.substr(10, connection.remember_password ? separator - 10
                                                           : std::string::npos);
      if (connection.remember_password) {
        connection.password = AES_decrypt(filename.substr(separator + 1),
                                          aes128_key_, aes128_iv_);
      }
      if (!WriteRecord(connection)) {
        result = -1;
        recent_connections.emplace_back(ConnectionKey(connection), connection);
        continue;
      }
      const auto metadata =
          std::filesystem::path(save_path_) / (remote_id + ".json");
      std::filesystem::last_write_time(metadata,
                                       std::filesystem::last_write_time(path));
      records.emplace(remote_id, connection);
    }
    auto& connection = records.at(remote_id);
    if (save_previews_ && connection.image_path.empty()) {
      const auto image =
          std::filesystem::path(save_path_) / (remote_id + ".png");
      std::error_code error;
      std::filesystem::copy_file(
          path, image, std::filesystem::copy_options::overwrite_existing,
          error);
      if (error) {
        // Keep the original preview available and retry migration next load.
        connection.image_path = path;
        result = -1;
        continue;
      }
      connection.image_path = image;
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) result = -1;
  }
  // Sort by metadata timestamps, unaffected by preview deletion or migration.
  for (const auto& path : FindThumbnailPath(save_path_)) {
    if (path.extension() == ".json") {
      const auto it = records.find(path.stem().string());
      if (it != records.end()) {
        recent_connections.emplace_back(ConnectionKey(it->second), it->second);
      }
    } else if (!save_previews_ && path.extension() == ".png") {
      std::error_code error;
      std::filesystem::remove(path, error);
      if (error) result = -1;
    }
  }
  return result;
}

bool Thumbnail::DecodeImage(const std::filesystem::path& image_path,
                            std::vector<unsigned char>* rgba, int* width,
                            int* height) const {
  if (!rgba || !width || !height) {
    return false;
  }

  int channels = 0;
  unsigned char* pixels =
      stbi_load(image_path.string().c_str(), width, height, &channels, 4);
  if (!pixels || *width <= 0 || *height <= 0) {
    if (pixels) {
      stbi_image_free(pixels);
    }
    rgba->clear();
    return false;
  }

  const size_t byte_count =
      static_cast<size_t>(*width) * static_cast<size_t>(*height) * 4;
  rgba->assign(pixels, pixels + byte_count);
  stbi_image_free(pixels);
  return true;
}

int Thumbnail::DeleteThumbnail(const std::string& filename_keyword) {
  std::lock_guard lock(mutex_);
  const auto remote_id = filename_keyword.substr(0, 9);
  if (!IsRemoteId(remote_id)) return -1;
  for (const auto& entry : std::filesystem::directory_iterator(save_path_)) {
    const auto name = entry.path().filename().string();
    if (entry.is_regular_file() && name.size() > 9 &&
        name.substr(0, 9) == remote_id &&
        (name[9] == '.' || name[9] == 'Y' || name[9] == 'N')) {
      std::filesystem::remove(entry.path());
    }
  }
  return 0;
}

std::vector<std::filesystem::path> Thumbnail::FindThumbnailPath(
    const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> thumbnails_path;

  if (!std::filesystem::is_directory(directory)) {
    LOG_ERROR("No such directory [{}]", directory.string());
    return thumbnails_path;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file()) {
      thumbnails_path.push_back(entry.path());
    }
  }

  std::sort(thumbnails_path.begin(), thumbnails_path.end(),
            [](const std::filesystem::path& a, const std::filesystem::path& b) {
              return std::filesystem::last_write_time(a) >
                     std::filesystem::last_write_time(b);
            });

  return thumbnails_path;
}

int Thumbnail::DeleteAllFilesInDirectory() {
  std::lock_guard lock(mutex_);
  if (std::filesystem::exists(save_path_) &&
      std::filesystem::is_directory(save_path_)) {
    for (const auto& entry : std::filesystem::directory_iterator(save_path_)) {
      if (std::filesystem::is_regular_file(entry.status())) {
        std::filesystem::remove(entry.path());
      }
    }
    return 0;
  }
  return -1;
}

std::string Thumbnail::AES_encrypt(const std::string& plaintext,
                                   unsigned char* key, unsigned char* iv) {
  EVP_CIPHER_CTX* ctx;
  int len;
  int ciphertext_len;
  int ret = 0;
  std::vector<unsigned char> ciphertext(plaintext.size() + AES_BLOCK_SIZE);

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    LOG_ERROR("Error in EVP_CIPHER_CTX_new");
    return plaintext;
  }

  ret = EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_EncryptInit_ex");
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
  }

  ret = EVP_EncryptUpdate(
      ctx, ciphertext.data(), &len,
      reinterpret_cast<const unsigned char*>(plaintext.data()),
      (int)plaintext.size());
  if (1 != ret) {
    LOG_ERROR("Error in EVP_EncryptUpdate");
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
  }

  ciphertext_len = len;
  ret = EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_EncryptFinal_ex");
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
  }

  ciphertext_len += len;

  unsigned char hex_str[256];
  size_t hex_str_len = 0;
  ret = OPENSSL_buf2hexstr_ex((char*)hex_str, sizeof(hex_str), &hex_str_len,
                              ciphertext.data(), ciphertext_len, '\0');
  if (1 != ret) {
    LOG_ERROR("Error in OPENSSL_buf2hexstr_ex");
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
  }

  EVP_CIPHER_CTX_free(ctx);

  std::string str(reinterpret_cast<char*>(hex_str), hex_str_len);
  return str;
}

std::string Thumbnail::AES_decrypt(const std::string& ciphertext,
                                   unsigned char* key, unsigned char* iv) {
  unsigned char ciphertext_buf[256];
  size_t ciphertext_buf_len = 0;
  unsigned char plaintext[256];
  int plaintext_len = 0;
  int plaintext_final_len = 0;
  EVP_CIPHER_CTX* ctx;
  int ret = 0;

  ret = OPENSSL_hexstr2buf_ex(ciphertext_buf, sizeof(ciphertext_buf),
                              &ciphertext_buf_len, ciphertext.c_str(), '\0');
  if (1 != ret) {
    LOG_ERROR("Error in OPENSSL_hexstr2buf_ex");
    return ciphertext;
  }

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    LOG_ERROR("Error in EVP_CIPHER_CTX_new");
    return ciphertext;
  }

  ret = EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_DecryptInit_ex");

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
  }

  ret = EVP_DecryptUpdate(ctx, plaintext, &plaintext_len, ciphertext_buf,
                          (int)ciphertext_buf_len);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_DecryptUpdate");

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
  }

  ret =
      EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &plaintext_final_len);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_DecryptFinal_ex");

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
  }
  plaintext_len += plaintext_final_len;

  EVP_CIPHER_CTX_free(ctx);

  return std::string(reinterpret_cast<char*>(plaintext), plaintext_len);
}
}  // namespace crossdesk
