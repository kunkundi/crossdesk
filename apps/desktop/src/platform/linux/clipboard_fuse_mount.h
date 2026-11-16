/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-16
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CLIPBOARD_FUSE_MOUNT_H_
#define _CLIPBOARD_FUSE_MOUNT_H_

#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace crossdesk::clipboard_detail {

// Neither FUSE version's structs cross this boundary. Their headers and ABI
// adapters live in separate translation units; file/promise handling is shared.
class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual int Stat(const char* path, struct stat* out) const = 0;
  virtual int ReadDirectory(const char* path, const std::function<int(const char*)>& fill) const = 0;
  virtual int Open(const char* path, int flags, uint64_t& handle) = 0;
  virtual int Read(char* data, size_t size, off_t offset, uint64_t handle) = 0;
  virtual int Release(uint64_t handle) = 0;
};

class FuseMount {
 public:
  // The caller cancels pending file reads before destroying the mount. The
  // implementation unmounts, joins its worker, then unloads its runtime library.
  virtual ~FuseMount() = default;
};

// UI thread only, with at most one active clipboard mount. A failed attempt
// does not cancel the shared promise, so the other backend may still be tried.
std::unique_ptr<FuseMount> MountFuse2(FileSystem& files, const std::string& path, bool& available);
std::unique_ptr<FuseMount> MountFuse3(FileSystem& files, const std::string& path, bool& available);

}  // namespace crossdesk::clipboard_detail

#endif
