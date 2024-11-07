#include "thumbnail.h"

#include <chrono>
#include <fstream>
#include <map>

#include "libyuv.h"
#include "rd_log.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

  try {
    libyuv::I420Scale(src_y, video_width_, src_u, (video_width_ + 1) / 2, src_v,
                      (video_width_ + 1) / 2, video_width_, video_height_,
                      dst_y.get(), scaled_video_width_, dst_u.get(),
                      (scaled_video_width_ + 1) / 2, dst_v.get(),
                      (scaled_video_width_ + 1) / 2, scaled_video_width_,
                      scaled_video_height_, libyuv::kFilterBilinear);
  } catch (const std::exception& e) {
    LOG_ERROR("I420Scale failed: %s", e.what());
    return;
  }

  try {
    libyuv::I420ToABGR(
        dst_y.get(), scaled_video_width_, dst_u.get(),
        (scaled_video_width_ + 1) / 2, dst_v.get(),
        (scaled_video_width_ + 1) / 2, reinterpret_cast<uint8_t*>(rgba_buffer_),
        scaled_video_width_ * 4, scaled_video_width_, scaled_video_height_);
  } catch (const std::exception& e) {
    LOG_ERROR("I420ToRGBA failed: %s", e.what());
    return;
  }
}

Thumbnail::Thumbnail() { std::filesystem::create_directory(image_path_); }

Thumbnail::~Thumbnail() {
  if (rgba_buffer_) {
    delete[] rgba_buffer_;
    rgba_buffer_ = nullptr;
  }
}

int Thumbnail::SaveToThumbnail(const char* yuv420p, int width, int height,
                               const std::string& host_name,
                               const std::string& remote_id) {
  if (!rgba_buffer_) {
    rgba_buffer_ = new char[thumbnail_width_ * thumbnail_height_ * 4];
  }

  if (yuv420p) {
    ScaleYUV420pToABGR((char*)yuv420p, width, height, thumbnail_width_,
                       thumbnail_height_, rgba_buffer_);
    std::string image_name =
        image_path_ + "/" + host_name + "@" + remote_id + ".png";
    stbi_write_png(image_name.data(), thumbnail_width_, thumbnail_height_, 4,
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
    fprintf(stderr, "Failed to load image: %s\n", stbi_failure_reason());
    return false;
  }

  // ABGR
  SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
      (void*)image_data, image_width, image_height, channels * 8,
      channels * image_width, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
  if (surface == nullptr) {
    fprintf(stderr, "Failed to create SDL surface: %s\n", SDL_GetError());
    return false;
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture == nullptr)
    fprintf(stderr, "Failed to create SDL texture: %s\n", SDL_GetError());

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
  std::string image_extensions = ".png";

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

      if (entry.path().extension() == image_extensions) {
        thumbnails_sorted_by_write_time_[last_write_time] = entry.path();
      }

      for (const auto& pair : thumbnails_sorted_by_write_time_) {
        thumbnails_path.push_back(pair.second);
      }
    }
  }

  return thumbnails_path;
}

int Thumbnail::LoadThumbnail(SDL_Renderer* renderer, SDL_Texture** texture,
                             int* width, int* height) {
  std::vector<std::filesystem::path> image_path =
      FindThumbnailPath(image_path_);

  if (image_path.size() == 0) {
    LOG_INFO("No thumbnail saved", image_path_);
    return -1;
  } else {
    LoadTextureFromFile(image_path[0].string().c_str(), renderer, texture,
                        width, height);
    return 0;
  }
}