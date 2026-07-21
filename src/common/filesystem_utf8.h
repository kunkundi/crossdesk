#ifndef CROSSDESK_COMMON_FILESYSTEM_UTF8_H_
#define CROSSDESK_COMMON_FILESYSTEM_UTF8_H_

#include <filesystem>
#include <string>
#include <string_view>

namespace crossdesk {

inline std::filesystem::path PathFromUtf8(std::string_view value) {
#if defined(__cpp_char8_t)
  const auto *begin = reinterpret_cast<const char8_t *>(value.data());
  return std::filesystem::path(std::u8string(begin, begin + value.size()));
#else
  return std::filesystem::u8path(value.begin(), value.end());
#endif
}

} // namespace crossdesk

#endif // CROSSDESK_COMMON_FILESYSTEM_UTF8_H_
