/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-08
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CREDENTIAL_CACHE_H_
#define _CREDENTIAL_CACHE_H_

#include <cstddef>
#include <cstdint>

namespace crossdesk::credential_cache {
inline constexpr uint32_t kMagic = 0x33444358;
inline constexpr uint32_t kVersion = 3;

struct CacheV1 {
  char client_id_with_password[17];
  int language;
  // Reserved legacy slots: retain the on-disk cache layout.
  int legacy_video_quality;
  int legacy_video_frame_rate;
  int video_encode_format;
  bool legacy_enable_hardware_video_codec;
  bool enable_turn;
  bool enable_srtp;
  unsigned char key[16];
  unsigned char iv[16];
};

struct CacheV2 {
  char client_id_with_password[17];
  int language;
  // Reserved legacy slots: retain the on-disk cache layout.
  int legacy_video_quality;
  int legacy_video_frame_rate;
  int video_encode_format;
  bool legacy_enable_hardware_video_codec;
  bool enable_turn;
  bool enable_srtp;
  unsigned char key[16];
  unsigned char iv[16];
  char self_hosted_id[17];
};

struct CacheV3 {
  uint32_t magic;
  uint32_t version;
  CacheV2 base;
  char pending_identity[17];
  char pending_server_host[256];
  int pending_server_port;
  bool pending_self_hosted;
  char pending_request_id[64];
  uint32_t checksum;
};


inline uint32_t Checksum(const void* data, size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 16777619u; }
  return hash;
}
inline bool Valid(const CacheV3& cache) {
  return cache.magic == kMagic && cache.version == kVersion &&
      cache.checksum == Checksum(&cache, offsetof(CacheV3, checksum));
}
}  // namespace crossdesk::credential_cache

#endif