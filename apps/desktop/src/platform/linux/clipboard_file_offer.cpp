#include "clipboard.h"

#define FUSE_USE_VERSION 31
#include <SDL3/SDL_clipboard.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <unistd.h>

#include <charconv>
#include <cstring>
#include <map>
#include <thread>

namespace crossdesk {
namespace {

// URI lists contain a read-only virtual filesystem. Looking up names/sizes is
// local; opening a file for a paste is the first operation that requests bytes.
class FileMount {
 public:
  explicit FileMount(std::shared_ptr<ClipboardFilePromise> offer) : offer_(std::move(offer)) {}
  ~FileMount() {
    offer_->Cancel();
    if (fuse_) {
      fuse_exit(fuse_);
      fuse_unmount(fuse_);
      if (worker_.joinable()) worker_.join();
      fuse_destroy(fuse_);
    }
    std::error_code ec;
    if (!directory_.empty()) std::filesystem::remove(directory_, ec);
  }
  bool Start() {
    std::string path = (std::filesystem::temp_directory_path() / "crossdesk-paste-XXXXXX").string();
    if (!mkdtemp(path.data())) return false;
    directory_ = path;
    struct fuse_operations ops{};
    ops.getattr = [](const char* path, struct stat* out, struct fuse_file_info*) {
      return Self()->Stat(path, out);
    };
    ops.readdir = [](const char* path, void* buffer, fuse_fill_dir_t fill, off_t,
                     struct fuse_file_info*, enum fuse_readdir_flags) {
      auto* self = Self();
      if (!self->offer_->valid()) return -ENOENT;
      constexpr auto flags = static_cast<fuse_fill_dir_flags>(0);
      fill(buffer, ".", nullptr, 0, flags);
      fill(buffer, "..", nullptr, 0, flags);
      if (std::strcmp(path, "/") == 0) {
        for (size_t i = 0; i < self->offer_->entries().size(); ++i)
          if (fill(buffer, std::to_string(i).c_str(), nullptr, 0, flags)) break;
        return 0;
      }
      bool directory = false;
      const int index = self->Index(path, directory);
      if (index < 0 || !directory) return -ENOENT;
      fill(buffer, self->offer_->entries()[index].name.c_str(), nullptr, 0, flags);
      return 0;
    };
    ops.open = [](const char* path, struct fuse_file_info* info) {
      auto* self = Self();
      bool directory = false;
      const int index = self->Index(path, directory);
      if (index < 0 || directory) return -ENOENT;
      if ((info->flags & O_ACCMODE) != O_RDONLY) return -EACCES;
      const auto files = self->offer_->Wait();
      if (!files || static_cast<size_t>(index) >= files->paths.size()) return -EIO;
      auto opened = std::make_shared<Opened>();
      opened->files = files;
      opened->fd = ::open(files->paths[index].c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
      if (opened->fd < 0) return -errno;
      std::lock_guard lock(self->mutex_);
      info->fh = ++self->next_;
      self->opened_[info->fh] = std::move(opened);
      info->keep_cache = 1;
      return 0;
    };
    ops.read = [](const char*, char* data, size_t size, off_t offset, struct fuse_file_info* info) {
      auto* self = Self();
      std::shared_ptr<Opened> opened;
      {
        std::lock_guard lock(self->mutex_);
        const auto found = self->opened_.find(info->fh);
        if (found != self->opened_.end()) opened = found->second;
      }
      if (!opened) return -EBADF;
      const auto count = pread(opened->fd, data, size, offset);
      return count < 0 ? -errno : static_cast<int>(count);
    };
    ops.release = [](const char*, struct fuse_file_info* info) {
      auto* self = Self();
      std::lock_guard lock(self->mutex_);
      self->opened_.erase(info->fh);
      return 0;
    };
    char program[] = "crossdesk-clipboard", option[] = "-o";
    char flags[] = "ro,nodev,nosuid,noexec,default_permissions,fsname=crossdesk-clipboard";
    char* argv[] = {program, option, flags};
    struct fuse_args args = FUSE_ARGS_INIT(3, argv);
    fuse_ = fuse_new(&args, &ops, sizeof(ops), this);
    fuse_opt_free_args(&args);
    if (!fuse_) return false;
    if (fuse_mount(fuse_, directory_.c_str()) != 0) {
      fuse_destroy(fuse_);
      fuse_ = nullptr;
      return false;
    }
    worker_ = std::thread([this] { fuse_loop_mt(fuse_, 0); });
    for (size_t i = 0; i < offer_->entries().size(); ++i)
      paths_.push_back(directory_ + "/" + std::to_string(i) + "/" + offer_->entries()[i].name);
    return true;
  }
  const auto& paths() const { return paths_; }
  const auto& offer() const { return offer_; }

 private:
  struct Opened {
    int fd = -1;
    std::shared_ptr<ClipboardFilePromise::Files> files;
    ~Opened() {
      if (fd >= 0) close(fd);
    }
  };
  static FileMount* Self() { return static_cast<FileMount*>(fuse_get_context()->private_data); }
  int Index(const char* path, bool& directory) const {
    if (!offer_->valid() || !path || *path != '/') return -1;
    std::string_view value(path + 1);
    const auto slash = value.find('/');
    const auto number = value.substr(0, slash);
    size_t index = 0;
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
        index >= offer_->entries().size())
      return -1;
    directory = slash == std::string_view::npos;
    if (!directory && value.substr(slash + 1) != offer_->entries()[index].name) return -1;
    return static_cast<int>(index);
  }
  int Stat(const char* path, struct stat* out) const {
    if (!offer_->valid()) return -ENOENT;
    *out = {};
    out->st_uid = getuid();
    out->st_gid = getgid();
    out->st_mode = S_IFDIR | 0500;
    out->st_nlink = 2;
    if (std::strcmp(path, "/") == 0) return 0;
    bool directory = false;
    const int index = Index(path, directory);
    if (index < 0) return -ENOENT;
    if (!directory) {
      out->st_mode = S_IFREG | 0400;
      out->st_nlink = 1;
      out->st_size = static_cast<off_t>(offer_->entries()[index].size);
    }
    return 0;
  }
  std::shared_ptr<ClipboardFilePromise> offer_;
  struct fuse* fuse_ = nullptr;
  std::thread worker_;
  std::string directory_;
  std::vector<std::string> paths_;
  std::mutex mutex_;
  uint64_t next_ = 0;
  std::map<uint64_t, std::shared_ptr<Opened>> opened_;
};
std::unique_ptr<FileMount> current;

}  // namespace

bool Clipboard::SetFileOffer(std::shared_ptr<ClipboardFilePromise> offer) {
  if (!offer || offer->entries().empty()) return false;
  ClearFileOffer();
  auto mount = std::make_unique<FileMount>(std::move(offer));
  if (!mount->Start() || !SetFiles(mount->paths())) return false;
  current = std::move(mount);
  return true;
}
bool Clipboard::OwnsFileOffer(const std::shared_ptr<ClipboardFilePromise>& offer) {
  if (!current || current->offer() != offer) return false;
  const auto paths = GetFiles();
  return paths && *paths == current->paths();
}
void Clipboard::ClearFileOffer() {
  if (current && OwnsFileOffer(current->offer())) SDL_ClearClipboardData();
  current.reset();
}

}  // namespace crossdesk
