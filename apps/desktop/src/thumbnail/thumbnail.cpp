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
#include <cmath>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "libyuv.h"
#include "rd_log.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace crossdesk {
namespace {

// Keep enough detail for high-DPI cards, independently of the display used
// when the session closes. Preserve the source aspect ratio without bars.
constexpr int kPreviewMaxEdge = 1024;

bool ScaleNv12ToABGR(const char* src, int src_w, int src_h, int dst_w,
                     int dst_h, uint8_t* dst_rgba) {
  const auto* y = reinterpret_cast<const uint8_t*>(src);
  const auto* uv = y + size_t(src_w) * src_h;
  const int src_cw = (src_w + 1) / 2, src_ch = (src_h + 1) / 2;
  const int dst_cw = (dst_w + 1) / 2, dst_ch = (dst_h + 1) / 2;
  std::vector<uint8_t> y_i420(size_t(src_w) * src_h);
  std::vector<uint8_t> u_i420(size_t(src_cw) * src_ch);
  std::vector<uint8_t> v_i420(u_i420.size());
  if (libyuv::NV12ToI420(y, src_w, uv, src_cw * 2, y_i420.data(), src_w,
                         u_i420.data(), src_cw, v_i420.data(), src_cw,
                         src_w, src_h) != 0)
    return false;

  std::vector<uint8_t> y_fit(size_t(dst_w) * dst_h);
  std::vector<uint8_t> u_fit(size_t(dst_cw) * dst_ch);
  std::vector<uint8_t> v_fit(u_fit.size());
  if (libyuv::I420Scale(y_i420.data(), src_w, u_i420.data(), src_cw,
                        v_i420.data(), src_cw, src_w, src_h, y_fit.data(), dst_w,
                        u_fit.data(), dst_cw, v_fit.data(), dst_cw, dst_w,
                        dst_h, libyuv::kFilterBox) != 0)
    return false;
  return libyuv::I420ToABGR(y_fit.data(), dst_w, u_fit.data(), dst_cw,
                           v_fit.data(), dst_cw, dst_rgba, dst_w * 4,
                           dst_w, dst_h) == 0;
}

}  // namespace

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

namespace {

bool IsRemoteId(const std::string& id) {
  return id.size() == 9 &&
         std::all_of(id.begin(), id.end(),
                     [](unsigned char c) { return c >= '0' && c <= '9'; });
}

bool IsLanRecordId(const std::string& id) {
  return id.size() == 68 && id.rfind("lan-", 0) == 0 &&
         std::all_of(id.begin() + 4, id.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
std::string ConnectionKey(const Thumbnail::RecentConnection& connection) {
  if (!connection.lan.device_id.empty()) return connection.remote_id;
  return connection.remote_id + (connection.remember_password ? "Y" : "N") +
         connection.remote_host_name +
         (connection.remember_password ? "@" + connection.password : "");
}

}  // namespace

std::string Thumbnail::LanRecordId(const std::string& device_id) {
  unsigned char digest[32]{};
  unsigned int size = 0;
  const std::string input = "lan_direct/" + device_id;
  if (EVP_Digest(input.data(), input.size(), digest, &size, EVP_sha256(),
                 nullptr) != 1 ||
      size != 32)
    return {};
  std::ostringstream text;
  text << "lan-" << std::hex << std::setfill('0');
  for (const auto byte : digest) text << std::setw(2) << unsigned(byte);
  return text.str();
}

bool Thumbnail::WriteRecord(const RecentConnection& connection) {
  const auto password = connection.remember_password
      ? AES_encrypt(connection.password, aes128_key_, aes128_iv_) : "";
  if (connection.remember_password && password.empty()) return false;
  const auto path =
      std::filesystem::path(save_path_) / (connection.remote_id + ".json");
  const auto temporary = path.string() + ".tmp";
  const nlohmann::json record = {
      {"version", 2},
      {"kind", connection.lan.device_id.empty() ? "cloud_id" : "lan_direct"},
      {"lan",
       {{"address", connection.lan.address},
        {"port", connection.lan.port},
        {"device_id", connection.lan.device_id}}},
      {"remote_id", connection.remote_id},
      {"host_name", connection.remote_host_name},
      {"password", password},
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
    const auto kind = record.value("kind", "cloud_id");
    if (kind == "lan_direct") {
      const auto& target = record.at("lan");
      connection->lan = {target.at("address"), target.at("device_id"),
                         target.at("port")};
      if (!IsLanRecordId(connection->remote_id) ||
          connection->lan.device_id.size() != 32 ||
          connection->remote_id != LanRecordId(connection->lan.device_id) ||
          !connection->lan.port || connection->lan.port > 65535 ||
          connection->lan.address.empty() ||
          connection->lan.address.size() > 128)
        return false;
    } else if (kind != "cloud_id" || !IsRemoteId(connection->remote_id))
      return false;
    if (path.stem() != connection->remote_id) return false;
    connection->remote_host_name = record.value("host_name", "");
    const auto password = record.value("password", "");
    connection->password =
        password.empty() ? "" : AES_decrypt(password, aes128_key_, aes128_iv_);
    connection->remember_password = !connection->password.empty();
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
                               HostPlatform platform, const LanTarget* lan) {
  std::lock_guard lock(mutex_);
  // Successful connections are history even without saved credentials.
  if (lan ? (!IsLanRecordId(remote_id) ||
             remote_id != LanRecordId(lan->device_id))
          : !IsRemoteId(remote_id))
    return -1;
  RecentConnection connection;
  ReadRecord(std::filesystem::path(save_path_) / (remote_id + ".json"),
             &connection);
  connection.remote_id = remote_id;
  if (!host_name.empty()) connection.remote_host_name = host_name;
  connection.lan = lan ? *lan : LanTarget{};
  connection.password = password;
  connection.remember_password = !connection.password.empty();
  if (platform != HostPlatform::Unknown) connection.platform = platform;
  if (!WriteRecord(connection)) return -1;

  const auto image = std::filesystem::path(save_path_) / (remote_id + ".png");
  if (save_previews_ && nv12 && width > 0 && height > 0) {
    const double scale = std::min(1.0, double(kPreviewMaxEdge) /
                                          std::max(width, height));
    const int image_width = std::max(1, int(std::lround(width * scale)));
    const int image_height = std::max(1, int(std::lround(height * scale)));
    std::vector<uint8_t> rgba(size_t(image_width) * image_height * 4);
    if (!ScaleNv12ToABGR(nv12, width, height, image_width, image_height,
                         rgba.data()) ||
        !stbi_write_png(image.string().c_str(), image_width, image_height, 4,
                         rgba.data(), image_width * 4))
      return -1;
  } else if (!save_previews_) {
    std::error_code error;
    std::filesystem::remove(image, error);
    if (error) return -1;
  }
  return 0;
}

int Thumbnail::LoadThumbnail(
    std::vector<std::pair<std::string, RecentConnection>>& recent_connections) {
  std::lock_guard lock(mutex_);
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
                            int target_width, int target_height,
                            std::vector<unsigned char>* rgba, int* width,
                            int* height) const {
  if (!rgba || !width || !height || target_width <= 0 || target_height <= 0) {
    return false;
  }
  rgba->clear();
  *width = *height = 0;
  int source_width = 0, source_height = 0, channels = 0;
  std::unique_lock lock(mutex_);
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load(image_path.string().c_str(), &source_width, &source_height,
                &channels, 4), stbi_image_free);
  lock.unlock();
  if (!pixels || source_width <= 0 || source_height <= 0)
    return false;

  // Match image-fit: cover, cropping once from the original cached image.
  int crop_width = source_width, crop_height = source_height;
  if (int64_t(source_width) * target_height >
      int64_t(source_height) * target_width)
    crop_width = std::max(1, int(int64_t(source_height) * target_width /
                                target_height));
  else
    crop_height = std::max(1, int(int64_t(source_width) * target_height /
                                 target_width));
  const double scale = std::min({1.0, double(crop_width) / target_width,
                                double(crop_height) / target_height});
  const int output_width = std::max(1, int(std::lround(target_width * scale)));
  const int output_height = std::max(1, int(std::lround(target_height * scale)));
  const auto* cropped = pixels.get() +
      (size_t((source_height - crop_height) / 2) * source_width +
       (source_width - crop_width) / 2) * 4;

  // ARGBScale falls back to bilinear for general box downsampling. Scale
  // separate color planes to get area filtering without another YUV conversion.
  const size_t source_area = size_t(crop_width) * crop_height;
  const size_t output_area = size_t(output_width) * output_height;
  std::vector<uint8_t> source_planes(source_area * 4);
  std::vector<uint8_t> output_planes(output_area * 4);
  libyuv::SplitARGBPlane(cropped, source_width * 4,
                         source_planes.data(), crop_width,
                         source_planes.data() + source_area, crop_width,
                         source_planes.data() + source_area * 2, crop_width,
                         source_planes.data() + source_area * 3, crop_width,
                         crop_width, crop_height);
  for (size_t channel = 0; channel < 4; ++channel) {
    if (libyuv::ScalePlane(source_planes.data() + source_area * channel,
                           crop_width, crop_width, crop_height,
                           output_planes.data() + output_area * channel,
                           output_width, output_width, output_height,
                           libyuv::kFilterBox) != 0)
      return false;
  }
  rgba->resize(output_area * 4);
  libyuv::MergeARGBPlane(output_planes.data(), output_width,
                         output_planes.data() + output_area, output_width,
                         output_planes.data() + output_area * 2, output_width,
                         output_planes.data() + output_area * 3, output_width,
                         rgba->data(), output_width * 4,
                         output_width, output_height);
  *width = output_width;
  *height = output_height;
  return true;
}

int Thumbnail::DeleteThumbnail(const std::string& filename_keyword) {
  std::lock_guard lock(mutex_);
  if (IsLanRecordId(filename_keyword)) {
    for (const auto* extension : {".json", ".png"})
      std::filesystem::remove(std::filesystem::path(save_path_) /
                              (filename_keyword + extension));
    return 0;
  }
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
    LOG_ERROR("Thumbnail directory does not exist");
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
    return {};
  }

  ret = EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_EncryptInit_ex");
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }

  ret = EVP_EncryptUpdate(
      ctx, ciphertext.data(), &len,
      reinterpret_cast<const unsigned char*>(plaintext.data()),
      (int)plaintext.size());
  if (1 != ret) {
    LOG_ERROR("Error in EVP_EncryptUpdate");
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }

  ciphertext_len = len;
  ret = EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_EncryptFinal_ex");
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }

  ciphertext_len += len;

  unsigned char hex_str[256];
  size_t hex_str_len = 0;
  ret = OPENSSL_buf2hexstr_ex((char*)hex_str, sizeof(hex_str), &hex_str_len,
                              ciphertext.data(), ciphertext_len, '\0');
  if (1 != ret) {
    LOG_ERROR("Error in OPENSSL_buf2hexstr_ex");
    EVP_CIPHER_CTX_free(ctx);
    return {};
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
    return {};
  }

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    LOG_ERROR("Error in EVP_CIPHER_CTX_new");
    return {};
  }

  ret = EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_DecryptInit_ex");

    EVP_CIPHER_CTX_free(ctx);
    return {};
  }

  ret = EVP_DecryptUpdate(ctx, plaintext, &plaintext_len, ciphertext_buf,
                          (int)ciphertext_buf_len);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_DecryptUpdate");

    EVP_CIPHER_CTX_free(ctx);
    return {};
  }

  ret =
      EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &plaintext_final_len);
  if (1 != ret) {
    LOG_ERROR("Error in EVP_DecryptFinal_ex");

    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  plaintext_len += plaintext_final_len;

  EVP_CIPHER_CTX_free(ctx);

  return std::string(reinterpret_cast<char*>(plaintext), plaintext_len);
}
}  // namespace crossdesk
