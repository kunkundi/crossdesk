#include "clipboard_fuse_mount.h"

#define FUSE_USE_VERSION 31
#include <dlfcn.h>
#include <fuse3/fuse.h>

#include <thread>

#include "rd_log.h"

namespace crossdesk::clipboard_detail {
namespace {

// Only one clipboard mount is active. This pointer is installed before its
// worker starts; the owning mount retains the library until the worker joins.
decltype(&fuse_get_context) get_context = nullptr;
FileSystem* Self() { return static_cast<FileSystem*>(get_context()->private_data); }

class Fuse3Mount final : public FuseMount {
 public:
  ~Fuse3Mount() override {
    if (fuse_) {
      exit_(fuse_);
      if (mounted_) unmount_(fuse_);
      if (worker_.joinable()) worker_.join();
      destroy_(fuse_);
    }
    if (library_) dlclose(library_);
  }
  bool Load() {
    // 3.17 restored the earlier ABI and changed SONAME to .4. We use only
    // the original operations/file-info fields, not the changed fuse_config.
    for (const char* name : {"libfuse3.so.4", "libfuse3.so.3"}) {
      library_ = dlopen(name, RTLD_NOW | RTLD_LOCAL);
      if (!library_) continue;
      // New runtimes export the 3.1 compatibility entry point by name; older
      // runtimes expose it as the default version of fuse_new instead.
      create_ = reinterpret_cast<Create>(dlsym(library_, "fuse_new_31"));
      if (!create_) create_ = reinterpret_cast<Create>(dlsym(library_, "fuse_new"));
      if (create_ && Symbol(free_args_, "fuse_opt_free_args") && Symbol(mount_, "fuse_mount") &&
          Symbol(loop_, "fuse_loop_mt_31") && Symbol(context_, "fuse_get_context") &&
          Symbol(exit_, "fuse_exit") && Symbol(unmount_, "fuse_unmount") &&
          Symbol(destroy_, "fuse_destroy")) return true;
      LOG_WARN("Clipboard file paste cannot use incompatible FUSE runtime {}", name);
      dlclose(library_);
      library_ = nullptr;
    }
    return false;
  }
  bool Start(FileSystem& files, const std::string& path) {
    struct fuse_operations ops{};
    ops.getattr = [](const char* path, struct stat* out, struct fuse_file_info*) {
      return Self()->Stat(path, out);
    };
    ops.readdir = [](const char* path, void* buffer, fuse_fill_dir_t fill, off_t,
                     struct fuse_file_info*, enum fuse_readdir_flags) {
      return Self()->ReadDirectory(path, [&](const char* name) {
        return fill(buffer, name, nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
      });
    };
    ops.open = [](const char* path, struct fuse_file_info* info) {
      const int result = Self()->Open(path, info->flags, info->fh);
      if (result == 0) info->keep_cache = 1;
      return result;
    };
    ops.read = [](const char*, char* data, size_t size, off_t offset, struct fuse_file_info* info) {
      return Self()->Read(data, size, offset, info->fh);
    };
    ops.release = [](const char*, struct fuse_file_info* info) { return Self()->Release(info->fh); };
    char program[] = "crossdesk-clipboard", option[] = "-o";
    char flags[] = "ro,nodev,nosuid,noexec,default_permissions,fsname=crossdesk-clipboard";
    char* argv[] = {program, option, flags};
    struct fuse_args args = FUSE_ARGS_INIT(3, argv);
    get_context = context_;
    fuse_ = create_(&args, &ops, sizeof(ops), &files);
    free_args_(&args);
    if (!fuse_) return false;
    if (mount_(fuse_, path.c_str()) != 0) return false;
    mounted_ = true;
    worker_ = std::thread([this] { loop_(fuse_, 0); });
    LOG_INFO("Clipboard file paste is using FUSE 3");
    return true;
  }

 private:
  template <typename T> bool Symbol(T& function, const char* name) {
    function = reinterpret_cast<T>(dlsym(library_, name));
    return function != nullptr;
  }
  using Create = struct fuse* (*)(struct fuse_args*, const struct fuse_operations*, size_t, void*);
  Create create_ = nullptr;
  decltype(&fuse_opt_free_args) free_args_ = nullptr;
  decltype(&fuse_mount) mount_ = nullptr;
  decltype(&fuse_loop_mt_31) loop_ = nullptr;
  decltype(&fuse_get_context) context_ = nullptr;
  decltype(&fuse_exit) exit_ = nullptr;
  decltype(&fuse_unmount) unmount_ = nullptr;
  decltype(&fuse_destroy) destroy_ = nullptr;
  void* library_ = nullptr;
  struct fuse* fuse_ = nullptr;
  bool mounted_ = false;
  std::thread worker_;
};

}  // namespace

std::unique_ptr<FuseMount> MountFuse3(FileSystem& files, const std::string& path, bool& available) {
  auto mount = std::make_unique<Fuse3Mount>();
  available = mount->Load();
  if (!available || !mount->Start(files, path)) return {};
  return mount;
}

}  // namespace crossdesk::clipboard_detail
