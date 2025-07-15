#include "thumbnail.h"

#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <chrono>
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

static std::string test;

bool LoadTextureFromMemory(const void* data, size_t data_size,
                           SDL_Renderer* renderer, SDL_Texture** out_texture,
                           int* out_width, int* out_height) {
  int image_width = 0;
  int image_height = 0;
  int channels = 4;
  unsigned char* image_data =
      stbi_load_from_memory((const unsigned char*)data, (int)data_size,
                            &image_width, &image_height, NULL, 4);
  if (image_data == nullptr) {
    LOG_ERROR("Failed to load image: [{}]", stbi_failure_reason());
    return false;
  }

  // ABGR
  SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
      (void*)image_data, image_width, image_height, channels * 8,
      channels * image_width, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
  if (surface == nullptr) {
    LOG_ERROR("Failed to create SDL surface: [{}]", SDL_GetError());
    return false;
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture == nullptr) {
    LOG_ERROR("Failed to create SDL texture: [{}]", SDL_GetError());
  }

  *out_texture = texture;
  *out_width = image_width;
  *out_height = image_height;

  SDL_FreeSurface(surface);
  stbi_image_free(image_data);

  return true;
}

bool LoadTextureFromFile(const char* file_name, SDL_Renderer* renderer,
                         SDL_Texture** out_texture, int* out_width,
                         int* out_height) {
  std::filesystem::path file_path(file_name);
  if (!std::filesystem::exists(file_path)) return false;
  std::ifstream file(file_path, std::ios::binary);
  if (!file) return false;
  file.seekg(0, std::ios::end);
  size_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);
  if (file_size == -1) return false;
  char* file_data = new char[file_size];
  if (!file_data) return false;
  file.read(file_data, file_size);
  bool ret = LoadTextureFromMemory(file_data, file_size, renderer, out_texture,
                                   out_width, out_height);
  delete[] file_data;

  return ret;
}

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

  memset(dst_rgba, 0, dst_w * dst_h * 4);
  for (int i = 0; i < dst_w * dst_h; ++i) {
    dst_rgba[i * 4 + 3] = 0xFF;
  }

  for (int y = 0; y < fit_h; ++y) {
    int dst_offset =
        ((y + (dst_h - fit_h) / 2) * dst_w + (dst_w - fit_w) / 2) * 4;
    memcpy(dst_rgba + dst_offset, abgr.data() + y * fit_w * 4, fit_w * 4);
  }
}

Thumbnail::Thumbnail() {
  RAND_bytes(aes128_key_, sizeof(aes128_key_));
  RAND_bytes(aes128_iv_, sizeof(aes128_iv_));
  std::filesystem::create_directory(image_path_);
}

Thumbnail::Thumbnail(unsigned char* aes128_key, unsigned char* aes128_iv) {
  memcpy(aes128_key_, aes128_key, sizeof(aes128_key_));
  memcpy(aes128_iv_, aes128_iv, sizeof(aes128_iv_));
  std::filesystem::create_directory(image_path_);
}

Thumbnail::~Thumbnail() {
  if (rgba_buffer_) {
    delete[] rgba_buffer_;
    rgba_buffer_ = nullptr;
  }
}

int Thumbnail::SaveToThumbnail(const char* yuv420p, int width, int height,
                               const std::string& remote_id,
                               const std::string& host_name,
                               const std::string& password) {
  if (!rgba_buffer_) {
    rgba_buffer_ = new char[thumbnail_width_ * thumbnail_height_ * 4];
  }

  if (yuv420p) {
    ScaleNv12ToABGR((char*)yuv420p, width, height, thumbnail_width_,
                    thumbnail_height_, rgba_buffer_);
  } else {
    // If yuv420p is null, fill the buffer with black pixels
    memset(rgba_buffer_, 0x00, thumbnail_width_ * thumbnail_height_ * 4);
    for (int i = 0; i < thumbnail_width_ * thumbnail_height_; ++i) {
      // Set alpha channel to opaque
      rgba_buffer_[i * 4 + 3] = 0xFF;
    }
  }

  std::string image_file_name;
  if (password.empty()) {
    return 0;
  } else {
    // delete the old thumbnail
    std::string filename_with_remote_id = remote_id;
    DeleteThumbnail(filename_with_remote_id);
  }

  std::string cipher_password = AES_encrypt(password, aes128_key_, aes128_iv_);
  image_file_name = remote_id + 'Y' + host_name + '@' + cipher_password;
  std::string file_path = image_path_ + image_file_name;
  stbi_write_png(file_path.data(), thumbnail_width_, thumbnail_height_, 4,
                 rgba_buffer_, thumbnail_width_ * 4);

  return 0;
}

int Thumbnail::LoadThumbnail(
    SDL_Renderer* renderer,
    std::unordered_map<std::string, RecentConnection>& recent_connections,
    int* width, int* height) {
  for (auto& it : recent_connections) {
    if (it.second.texture != nullptr) {
      SDL_DestroyTexture(it.second.texture);
      it.second.texture = nullptr;
    }
  }
  recent_connections.clear();

  std::vector<std::filesystem::path> image_paths =
      FindThumbnailPath(image_path_);

  if (image_paths.size() == 0) {
    return -1;
  } else {
    for (int i = 0; i < image_paths.size(); i++) {
      size_t pos1 = image_paths[i].string().find('/') + 1;
      std::string cipher_image_name = image_paths[i].string().substr(pos1);
      std::string remote_id;
      std::string cipher_password;
      std::string remote_host_name;
      std::string original_image_name;

      if ('Y' == cipher_image_name[9] && cipher_image_name.size() >= 16) {
        size_t pos_y = cipher_image_name.find('Y');
        size_t pos_at = cipher_image_name.find('@');

        if (pos_y == std::string::npos || pos_at == std::string::npos ||
            pos_y >= pos_at) {
          LOG_ERROR("Invalid filename");
          continue;
        }

        remote_id = cipher_image_name.substr(0, pos_y);
        remote_host_name =
            cipher_image_name.substr(pos_y + 1, pos_at - pos_y - 1);
        cipher_password = cipher_image_name.substr(pos_at + 1);

        original_image_name =
            remote_id + 'Y' + remote_host_name + "@" +
            AES_decrypt(cipher_password, aes128_key_, aes128_iv_);
      } else {
        size_t pos_n = cipher_image_name.find('N');
        size_t pos_at = cipher_image_name.find('@');

        if (pos_n == std::string::npos) {
          LOG_ERROR("Invalid filename");
          continue;
        }

        remote_id = cipher_image_name.substr(0, pos_n);
        remote_host_name = cipher_image_name.substr(pos_n + 1);

        original_image_name =
            remote_id + 'N' + remote_host_name + "@" +
            AES_decrypt(cipher_password, aes128_key_, aes128_iv_);
      }

      std::string image_path = image_path_ + cipher_image_name;
      recent_connections[original_image_name].texture = nullptr;
      LoadTextureFromFile(image_path.c_str(), renderer,
                          &(recent_connections[original_image_name].texture),
                          width, height);
    }
    return 0;
  }
  return 0;
}

int Thumbnail::DeleteThumbnail(const std::string& filename_keyword) {
  for (const auto& entry : std::filesystem::directory_iterator(image_path_)) {
    if (entry.is_regular_file()) {
      const std::string filename = entry.path().filename().string();
      std::string id_hostname = filename_keyword.substr(0, filename.find('@'));
      if (filename.find(id_hostname) != std::string::npos) {
        std::filesystem::remove(entry.path());
      }
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
  if (std::filesystem::exists(image_path_) &&
      std::filesystem::is_directory(image_path_)) {
    for (const auto& entry : std::filesystem::directory_iterator(image_path_)) {
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
