#include "runtime/device_identity_store.h"

#include <openssl/crypto.h>

#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "private_file.h"

namespace crossdesk::device_identity_store {
namespace {
void Require(bool ok) {
  if (!ok) throw std::runtime_error("Device identity store unavailable or invalid");
}

// Keep the lock file's inode stable; it contains no credentials.
class StoreLock {
 public:
  explicit StoreLock(const std::filesystem::path& path) {
#ifdef _WIN32
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle_ == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle_, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
      if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
      throw std::runtime_error("Device identity store unavailable");
    }
#else
    handle_ = open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    struct stat info{};
    if (handle_ < 0 || fstat(handle_, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != getuid() || (info.st_mode & 0077) != 0 ||
        flock(handle_, LOCK_EX | LOCK_NB) != 0) {
      if (handle_ >= 0) close(handle_);
      throw std::runtime_error("Device identity store unavailable");
    }
#endif
  }
  ~StoreLock() {
#ifdef _WIN32
    CloseHandle(handle_);
#else
    close(handle_);
#endif
  }
  StoreLock(const StoreLock&) = delete;
  StoreLock& operator=(const StoreLock&) = delete;

 private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int handle_ = -1;
#endif
};

struct PrivateText {
  std::string value;
  ~PrivateText() { OPENSSL_cleanse(value.data(), value.size()); }
};

void Validate(const nlohmann::json& document) {
  Require(document.is_object() && document.at("version") == 1);
}
}  // namespace

void Update(const std::filesystem::path& cache,
            const std::function<void(nlohmann::json&)>& update) {
  private_file::EnsureDirectory(cache, true);
  StoreLock lock(cache / "identity.json.lock");
  const auto path = cache / "identity.json";
  const bool exists = std::filesystem::symlink_status(path).type() !=
                      std::filesystem::file_type::not_found;
  nlohmann::json saved = {{"version", 1}};
  if (exists) {
    PrivateText text{private_file::Read(path)};
    saved = nlohmann::json::parse(text.value, nullptr, false);
    Require(!saved.is_discarded());
  }
  Validate(saved);
  PrivateText before{saved.dump()};
  update(saved);
  Validate(saved);
  PrivateText after{saved.dump()};
  Require(after.value.size() <= 64 * 1024);
  if (!exists || before.value != after.value) private_file::Write(path, after.value);
}

}  // namespace crossdesk::device_identity_store
