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

void ScaleYUV420pToABGR(char* dst_buffer_, int video_width_, int video_height_,
                        int scaled_video_width_, int scaled_video_height_,
                        char* rgba_buffer_) {
  int src_y_size = video_width_ * video_height_;
  int src_uv_size = (video_width_ + 1) / 2 * (video_height_ + 1) / 2;
  int dst_y_size = scaled_video_width_ * scaled_video_height_;
  int dst_uv_size =
      (scaled_video_width_ + 1) / 2 * (scaled_video_height_ + 1) / 2;

  uint8_t* src_y = reinterpret_cast<uint8_t*>(dst_buffer_);
  uint8_t* src_u = src_y + src_y_size;
  uint8_t* src_v = src_u + src_uv_size;

  std::unique_ptr<uint8_t[]> dst_y(new uint8_t[dst_y_size]);
  std::unique_ptr<uint8_t[]> dst_u(new uint8_t[dst_uv_size]);
  std::unique_ptr<uint8_t[]> dst_v(new uint8_t[dst_uv_size]);

  libyuv::I420Scale(src_y, video_width_, src_u, (video_width_ + 1) / 2, src_v,
                    (video_width_ + 1) / 2, video_width_, video_height_,
                    dst_y.get(), scaled_video_width_, dst_u.get(),
                    (scaled_video_width_ + 1) / 2, dst_v.get(),
                    (scaled_video_width_ + 1) / 2, scaled_video_width_,
                    scaled_video_height_, libyuv::kFilterBilinear);

  libyuv::I420ToABGR(
      dst_y.get(), scaled_video_width_, dst_u.get(),
      (scaled_video_width_ + 1) / 2, dst_v.get(), (scaled_video_width_ + 1) / 2,
      reinterpret_cast<uint8_t*>(rgba_buffer_), scaled_video_width_ * 4,
      scaled_video_width_, scaled_video_height_);
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
    ScaleYUV420pToABGR((char*)yuv420p, width, height, thumbnail_width_,
                       thumbnail_height_, rgba_buffer_);

    std::string image_name;
    if (password.empty()) {
      image_name = remote_id + 'N' + host_name;
    } else {
      // delete the file which has no password in its name
      std::string filename_without_password = remote_id + "N" + host_name;
      DeleteThumbnail(filename_without_password);

      image_name = remote_id + 'Y' + password + host_name;
    }

    std::string ciphertext = AES_encrypt(image_name, aes128_key_, aes128_iv_);
    std::string file_path = image_path_ + ciphertext;
    stbi_write_png(file_path.data(), thumbnail_width_, thumbnail_height_, 4,
                   rgba_buffer_, thumbnail_width_ * 4);
  }
  return 0;
}

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

std::vector<std::filesystem::path> Thumbnail::FindThumbnailPath(
    const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> thumbnails_path;

  if (!std::filesystem::is_directory(directory)) {
    LOG_ERROR("No such directory [{}]", directory.string());
    return thumbnails_path;
  }

  thumbnails_sorted_by_write_time_.clear();

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file()) {
      std::time_t last_write_time = std::chrono::system_clock::to_time_t(
          time_point_cast<std::chrono::system_clock::duration>(
              entry.last_write_time() -
              std::filesystem::file_time_type::clock::now() +
              std::chrono::system_clock::now()));

      thumbnails_sorted_by_write_time_[last_write_time] = entry.path();
    }
  }

  for (auto it = thumbnails_sorted_by_write_time_.rbegin();
       it != thumbnails_sorted_by_write_time_.rend(); ++it) {
    thumbnails_path.push_back(it->second);
  }

  return thumbnails_path;
}

int Thumbnail::LoadThumbnail(SDL_Renderer* renderer,
                             std::map<std::string, SDL_Texture*>& textures,
                             int* width, int* height) {
  for (auto& it : textures) {
    if (it.second != nullptr) {
      SDL_DestroyTexture(it.second);
      it.second = nullptr;
    }
  }
  textures.clear();

  std::vector<std::filesystem::path> image_paths =
      FindThumbnailPath(image_path_);

  if (image_paths.size() == 0) {
    return -1;
  } else {
    for (int i = 0; i < image_paths.size(); i++) {
      size_t pos1 = image_paths[i].string().find('/') + 1;
      std::string cipher_image_name = image_paths[i].string().substr(pos1);
      std::string original_image_name =
          AES_decrypt(cipher_image_name, aes128_key_, aes128_iv_);
      std::string image_path = image_path_ + cipher_image_name;
      textures[original_image_name] = nullptr;
      LoadTextureFromFile(image_path.c_str(), renderer,
                          &(textures[original_image_name]), width, height);
    }
    return 0;
  }
  return 0;
}

int Thumbnail::DeleteThumbnail(const std::string& file_name) {
  std::string ciphertext = AES_encrypt(file_name, aes128_key_, aes128_iv_);
  std::string file_path = image_path_ + ciphertext;
  if (std::filesystem::exists(file_path)) {
    std::filesystem::remove(file_path);
    return 0;
  } else {
    return -1;
  }
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
