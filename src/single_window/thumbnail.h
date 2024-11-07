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

class Thumbnail {
 public:
  Thumbnail();
  ~Thumbnail();

 public:
  int SaveToThumbnail(const char* yuv420p, int width, int height,
                      const std::string& host_name,
                      const std::string& remote_id);

  int LoadThumbnail(SDL_Renderer* renderer, SDL_Texture** texture, int* width,
                    int* height);

 private:
  std::vector<std::filesystem::path> FindThumbnailPath(
      const std::filesystem::path& directory);

 private:
  int thumbnail_width_ = 160;
  int thumbnail_height_ = 90;
  char* rgba_buffer_ = nullptr;
  std::string image_path_ = "thumbnails";
  std::map<std::time_t, std::filesystem::path> thumbnails_sorted_by_write_time_;
};

#endif