#include "clipboard.h"

#include <SDL3/SDL_clipboard.h>
#include <fcntl.h>
#include <unistd.h>

#include <charconv>
#include <cstring>
#include <map>

#include "clipboard_fuse_mount.h"
#include "rd_log.h"

namespace crossdesk {
namespace {

// URI lists contain a read-only virtual filesystem. Looking up names/sizes is
// local; opening a file requests its bytes from the peer.
class FileMount : public clipboard_detail::FileSystem {
 public:
  explicit FileMount(std::shared_ptr<ClipboardFilePromise> offer) : offer_(std::move(offer)) {}
  ~FileMount() override {
    offer_->Cancel();
    mount_.reset();
    std::error_code ec;
    if (!directory_.empty()) std::filesystem::remove(directory_, ec);
  }
  bool Start(Clipboard::FileOfferError& error) {
    std::string path = (std::filesystem::temp_directory_path() / "crossdesk-paste-XXXXXX").string();
    if (!mkdtemp(path.data())) return false;
    directory_ = path;
    bool fuse3_available = false, fuse2_available = false;
    mount_ = clipboard_detail::MountFuse3(*this, directory_, fuse3_available);
    if (!mount_) mount_ = clipboard_detail::MountFuse2(*this, directory_, fuse2_available);
    if (!mount_) {
      error = fuse3_available || fuse2_available ? Clipboard::FileOfferError::MountFailed
                                               : Clipboard::FileOfferError::UnavailableRuntime;
      LOG_WARN("Clipboard file paste requires a working FUSE 2 or 3 runtime and mount permissions");
      return false;
    }
    for (size_t i = 0; i < offer_->entries().size(); ++i)
      paths_.push_back(directory_ + "/" + std::to_string(i) + "/" + offer_->entries()[i].name);
    return true;
  }
  const auto& paths() const { return paths_; }
  const auto& offer() const { return offer_; }

  int ReadDirectory(const char* path, const std::function<int(const char*)>& fill) const override {
    if (!offer_->valid()) return -ENOENT;
    if (fill(".") || fill("..")) return 0;
    if (std::strcmp(path, "/") == 0) {
      for (size_t i = 0; i < offer_->entries().size(); ++i)
        if (fill(std::to_string(i).c_str())) break;
      return 0;
    }
    bool directory = false;
    const int index = Index(path, directory);
    if (index < 0 || !directory) return -ENOENT;
    fill(offer_->entries()[index].name.c_str());
    return 0;
  }
  int Open(const char* path, int flags, uint64_t& handle) override {
    bool directory = false;
    const int index = Index(path, directory);
    if (index < 0 || directory) return -ENOENT;
    if ((flags & O_ACCMODE) != O_RDONLY) return -EACCES;
    const auto files = offer_->Wait();
    if (!files || static_cast<size_t>(index) >= files->paths.size()) return -EIO;
    auto opened = std::make_shared<Opened>();
    opened->files = files;
    opened->fd = ::open(files->paths[index].c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (opened->fd < 0) return -errno;
    std::lock_guard lock(mutex_);
    handle = ++next_;
    opened_[handle] = std::move(opened);
    return 0;
  }
  int Read(char* data, size_t size, off_t offset, uint64_t handle) override {
    std::shared_ptr<Opened> opened;
    {
      std::lock_guard lock(mutex_);
      const auto found = opened_.find(handle);
      if (found != opened_.end()) opened = found->second;
    }
    if (!opened) return -EBADF;
    const auto count = pread(opened->fd, data, size, offset);
    return count < 0 ? -errno : static_cast<int>(count);
  }
  int Release(uint64_t handle) override {
    std::lock_guard lock(mutex_);
    opened_.erase(handle);
    return 0;
  }

 private:
  struct Opened {
    int fd = -1;
    std::shared_ptr<ClipboardFilePromise::Files> files;
    ~Opened() {
      if (fd >= 0) close(fd);
    }
  };
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
  int Stat(const char* path, struct stat* out) const override {
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
  std::unique_ptr<clipboard_detail::FuseMount> mount_;
  std::string directory_;
  std::vector<std::string> paths_;
  std::mutex mutex_;
  uint64_t next_ = 0;
  std::map<uint64_t, std::shared_ptr<Opened>> opened_;
};
std::unique_ptr<FileMount> current;

}  // namespace

bool Clipboard::SetFileOffer(std::shared_ptr<ClipboardFilePromise> offer, FileOfferError* error) {
  FileOfferError failure = FileOfferError::PublishFailed;
  if (!error) error = &failure;
  *error = FileOfferError::PublishFailed;
  if (!offer || offer->entries().empty()) return false;
  ClearFileOffer();
  auto mount = std::make_unique<FileMount>(std::move(offer));
  if (!mount->Start(*error) || !SetFiles(mount->paths())) return false;
  current = std::move(mount);
  *error = FileOfferError::NoError;
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
