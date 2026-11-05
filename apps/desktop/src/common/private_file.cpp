#include "private_file.h"

#include <openssl/rand.h>

#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <Aclapi.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace crossdesk::private_file {
namespace {
void Require(bool ok, const char* error) {
  if (!ok) throw std::runtime_error(error);
}
std::string RandomSuffix() {
  unsigned char bytes[16];
  Require(RAND_bytes(bytes, sizeof(bytes)) == 1, "Secure random source unavailable");
  constexpr char hex[] = "0123456789abcdef";
  std::string value;
  for (auto byte : bytes) { value += hex[byte >> 4]; value += hex[byte & 15]; }
  return value;
}
#ifdef _WIN32
std::vector<unsigned char> CurrentUser() {
  HANDLE token = nullptr;
  Require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token),
          "Cannot open user token");
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> data(size);
  bool ok = GetTokenInformation(token, TokenUser, data.data(), size, &size);
  CloseHandle(token);
  Require(ok, "Cannot read user token");
  return data;
}
PSECURITY_DESCRIPTOR PrivateSecurityDescriptor(bool directory) {
  auto user = CurrentUser();
  LPWSTR sid = nullptr;
  Require(ConvertSidToStringSidW(
              reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid),
          "Cannot read user SID");
  // An elevated token can default to Administrators as the owner even when
  // its DACL grants access only to TokenUser. Always set both explicitly.
  const auto acl = L"O:" + std::wstring(sid) + L"D:P(A;" +
                   (directory ? L"OICI" : L"") + L";FA;;;" + sid + L")";
  LocalFree(sid);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
              acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr),
          "Cannot protect private file");
  return descriptor;
}
#endif
}  // namespace

void Write(const std::filesystem::path& path, const std::string& text) {
  const auto temporary = path.parent_path() / (".write-" + RandomSuffix());
#ifdef _WIN32
  auto descriptor = PrivateSecurityDescriptor(false);
  SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, &security,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  LocalFree(descriptor);
  Require(file != INVALID_HANDLE_VALUE, "Cannot create private file");
  DWORD written = 0;
  bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
                      &written, nullptr) &&
            written == text.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    throw std::runtime_error("Cannot persist private file");
  }
#else
  int fd =
      open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  Require(fd >= 0, "Cannot create private file");
  size_t offset = 0;
  while (offset < text.size()) {
    const auto count = write(fd, text.data() + offset, text.size() - offset);
    if (count <= 0) break;
    offset += static_cast<size_t>(count);
  }
  bool ok = offset == text.size() && fsync(fd) == 0;
  close(fd);
  if (!ok || rename(temporary.c_str(), path.c_str()) != 0) {
    unlink(temporary.c_str());
    throw std::runtime_error("Cannot persist private file");
  }
  const int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  const bool durable = directory >= 0 && fsync(directory) == 0;
  if (directory >= 0) close(directory);
  Require(durable, "Cannot persist private directory entry");
#endif
}
namespace {
#ifdef _WIN32
using FileHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
FileHandle OpenPrivatePath(const std::filesystem::path& path, DWORD access) {
  // Keep the object open without FILE_SHARE_DELETE so validation and legacy
  // owner repair cannot operate on a replacement at the same path.
  HANDLE file = CreateFileW(
      path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr);
  Require(file != INVALID_HANDLE_VALUE, "Cannot open private path");
  return {file, CloseHandle};
}
void CheckPrivateWindowsPath(const std::filesystem::path& path) {
  auto file = OpenPrivatePath(path, READ_CONTROL);
  BY_HANDLE_FILE_INFORMATION info{};
  Require(GetFileInformationByHandle(file.get(), &info) &&
              !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
          "private path cannot be a reparse point");
  auto user = CurrentUser();
  auto sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
  PSID owner = nullptr;
  PACL acl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  Require(GetSecurityInfo(
              file.get(), SE_FILE_OBJECT,
              OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
              nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS,
          "Cannot read private file permissions");
  std::unique_ptr<void, decltype(&LocalFree)> security(descriptor, LocalFree);
  bool ok = acl && acl->AceCount > 0;
  // Only the exact protected, full-access DACL written by older versions is
  // eligible for owner repair. Never adopt a shared or foreign-user profile.
  SECURITY_DESCRIPTOR_CONTROL control{};
  DWORD revision = 0;
  bool legacy = owner && IsWellKnownSid(owner, WinBuiltinAdministratorsSid) &&
                GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                (control & SE_DACL_PROTECTED) && acl && acl->AceCount == 1;
  for (DWORD i = 0; ok && i < acl->AceCount; ++i) {
    void* raw = nullptr;
    if (!GetAce(acl, i, &raw)) {
      ok = false;
      break;
    }
    const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    ok = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
         EqualSid(const_cast<DWORD*>(&ace->SidStart), sid);
    const BYTE flags = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                           ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE
                           : 0;
    legacy =
        legacy && ace->Mask == FILE_ALL_ACCESS && ace->Header.AceFlags == flags;
  }
  Require(ok && owner && (EqualSid(owner, sid) || legacy),
          "private files must be accessible only by the current user");
  if (legacy) {
    auto writable = OpenPrivatePath(path, WRITE_OWNER);
    BY_HANDLE_FILE_INFORMATION current{};
    Require(GetFileInformationByHandle(writable.get(), &current) &&
                current.dwVolumeSerialNumber == info.dwVolumeSerialNumber &&
                current.nFileIndexHigh == info.nFileIndexHigh &&
                current.nFileIndexLow == info.nFileIndexLow &&
                SetSecurityInfo(writable.get(), SE_FILE_OBJECT,
                                OWNER_SECURITY_INFORMATION, sid, nullptr,
                                nullptr, nullptr) == ERROR_SUCCESS,
            "Cannot repair private file owner");
  }
}
void CreatePrivateDirectory(const std::filesystem::path& path, bool protect_existing) {
  if (!std::filesystem::exists(path)) {
    auto descriptor = PrivateSecurityDescriptor(true);
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    bool ok = CreateDirectoryW(path.c_str(), &security);
    LocalFree(descriptor);
    Require(ok, "Cannot create private directory");
  }
  if (protect_existing) {
    // The cache may predate identity storage and inherit a normal profile ACL.
    // Elevated older builds may have used Administrators as the owner. The
    // private-path check below repairs that owner after applying the user ACL.
    auto file = OpenPrivatePath(path, READ_CONTROL | WRITE_DAC);
    BY_HANDLE_FILE_INFORMATION info{};
    Require(GetFileInformationByHandle(file.get(), &info) &&
                (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
            "Invalid cache directory");
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR original = nullptr;
    Require(GetSecurityInfo(file.get(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                &owner, nullptr, nullptr, nullptr, &original) == ERROR_SUCCESS,
            "Cannot read cache directory owner");
    std::unique_ptr<void, decltype(&LocalFree)> original_guard(original, LocalFree);
    auto user = CurrentUser();
    Require(owner && (EqualSid(owner, reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid) ||
                     IsWellKnownSid(owner, WinBuiltinAdministratorsSid)),
            "Cache directory belongs to another user");
    auto descriptor = PrivateSecurityDescriptor(true);
    std::unique_ptr<void, decltype(&LocalFree)> descriptor_guard(descriptor, LocalFree);
    BOOL present = FALSE, defaulted = FALSE;
    PACL acl = nullptr;
    Require(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present &&
                SetSecurityInfo(file.get(), SE_FILE_OBJECT,
                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                    nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS,
            "Cannot protect cache directory");
  }
  CheckPrivateWindowsPath(path);
}
#endif
}  // namespace

std::string Read(const std::filesystem::path& path) {
#ifdef _WIN32
  CheckPrivateWindowsPath(path);
#else
  struct stat status{};
  Require(lstat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode) &&
              status.st_uid == getuid() && (status.st_mode & 0077) == 0,
          "private file must be owned by this user with mode 0600");
#endif
  Require(std::filesystem::file_size(path) <= 64 * 1024, "private file too large");
  std::ifstream input(path, std::ios::binary);
  Require(input.good(), "Cannot read private file");
  return {std::istreambuf_iterator<char>(input), {}};
}

void EnsureDirectory(const std::filesystem::path& directory, bool protect_existing) {
  const auto parent = directory.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
#ifdef _WIN32
  CreatePrivateDirectory(directory, protect_existing);
#else
  std::filesystem::create_directories(directory);
  struct stat status{};
  Require(lstat(directory.c_str(), &status) == 0 && S_ISDIR(status.st_mode) &&
              status.st_uid == getuid(), "Invalid private directory owner or type");
  Require(chmod(directory.c_str(), 0700) == 0, "Cannot protect private directory");
#endif
}
}  // namespace crossdesk::private_file
