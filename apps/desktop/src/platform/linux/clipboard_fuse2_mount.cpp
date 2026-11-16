#include "clipboard_fuse_mount.h"

#define FUSE_USE_VERSION 26
#include <dlfcn.h>
#include <fuse/fuse.h>

#include <thread>

#include "rd_log.h"

namespace crossdesk::clipboard_detail {
namespace {

decltype(&fuse_get_context) get_context = nullptr;
FileSystem* Self() { return static_cast<FileSystem*>(get_context()->private_data); }

class Fuse2Mount final : public FuseMount {
 public:
  ~Fuse2Mount() override {
    if (fuse_) exit_(fuse_);
    if (channel_) unmount_(path_.c_str(), channel_);
    if (worker_.joinable()) worker_.join();
    if (fuse_) destroy_(fuse_);
    if (library_) dlclose(library_);
  }
  bool Load() {
    library_ = dlopen("libfuse.so.2", RTLD_NOW | RTLD_LOCAL);
    return library_ && Symbol(create_, "fuse_new") && Symbol(free_args_, "fuse_opt_free_args") &&
        Symbol(mount_, "fuse_mount") && Symbol(loop_, "fuse_loop_mt") &&
        Symbol(context_, "fuse_get_context") && Symbol(exit_, "fuse_exit") &&
        Symbol(unmount_, "fuse_unmount") && Symbol(destroy_, "fuse_destroy");
  }
  bool Start(FileSystem& files, const std::string& path) {
    struct fuse_operations ops{};
    ops.getattr = [](const char* path, struct stat* out) { return Self()->Stat(path, out); };
    ops.readdir = [](const char* path, void* buffer, fuse_fill_dir_t fill, off_t,
                     struct fuse_file_info*) {
      return Self()->ReadDirectory(path, [&](const char* name) {
        return fill(buffer, name, nullptr, 0);
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
    path_ = path;
    // FUSE 2 mounts a channel first, then attaches it to fuse_new. FUSE 3
    // creates the filesystem first and mounts it directly instead.
    channel_ = mount_(path.c_str(), &args);
    if (channel_) fuse_ = create_(channel_, &args, &ops, sizeof(ops), &files);
    free_args_(&args);
    if (!fuse_) return false;
    worker_ = std::thread([this] { loop_(fuse_); });
    LOG_INFO("Clipboard file paste is using FUSE 2");
    return true;
  }

 private:
  template <typename T> bool Symbol(T& function, const char* name) {
    function = reinterpret_cast<T>(dlsym(library_, name));
    return function != nullptr;
  }
  decltype(&fuse_new) create_ = nullptr;
  decltype(&fuse_opt_free_args) free_args_ = nullptr;
  decltype(&fuse_mount) mount_ = nullptr;
  decltype(&fuse_loop_mt) loop_ = nullptr;
  decltype(&fuse_get_context) context_ = nullptr;
  decltype(&fuse_exit) exit_ = nullptr;
  decltype(&fuse_unmount) unmount_ = nullptr;
  decltype(&fuse_destroy) destroy_ = nullptr;
  void* library_ = nullptr;
  struct fuse* fuse_ = nullptr;
  struct fuse_chan* channel_ = nullptr;
  std::string path_;
  std::thread worker_;
};

}  // namespace

std::unique_ptr<FuseMount> MountFuse2(FileSystem& files, const std::string& path, bool& available) {
  auto mount = std::make_unique<Fuse2Mount>();
  available = mount->Load();
  if (!available || !mount->Start(files, path)) return {};
  return mount;
}

}  // namespace crossdesk::clipboard_detail
