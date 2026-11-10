#include "diagnostic_export.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "common/private_file.h"

namespace crossdesk::feedback {
namespace {
constexpr size_t kMaxSource = 8 * 1024 * 1024;

std::string TimeText(std::time_t value, const char* format, bool utc = false) {
  std::tm time{};
#ifdef _WIN32
  if (utc)
    gmtime_s(&time, &value);
  else
    localtime_s(&time, &value);
#else
  if (utc)
    gmtime_r(&value, &time);
  else
    localtime_r(&value, &time);
#endif
  std::ostringstream out;
  out << std::put_time(&time, format);
  return out.str();
}

struct Entry {
  std::string name, text;
  uint32_t crc = 0, offset = 0;
};

void Put(std::string& out, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) out += static_cast<char>(value >> (i * 8));
}
uint32_t Crc32(std::string_view text) {
  static const auto table = [] {
    std::array<uint32_t, 256> values{};
    for (uint32_t i = 0; i < values.size(); ++i) {
      uint32_t value = i;
      for (int bit = 0; bit < 8; ++bit)
        value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
      values[i] = value;
    }
    return values;
  }();
  uint32_t crc = 0xffffffffu;
  for (unsigned char byte : text) crc = table[(crc ^ byte) & 255] ^ (crc >> 8);
  return crc ^ 0xffffffffu;
}

// A bounded, stored ZIP needs neither ZIP64 nor an external archiver. Fixed
// ASCII entry names prevent exported paths from disclosing local directories.
std::string Zip(std::vector<Entry>& entries) {
  std::string out;
  for (auto& entry : entries) {
    entry.offset = static_cast<uint32_t>(out.size());
    entry.crc = Crc32(entry.text);
    Put(out, 0x04034b50, 4);
    Put(out, 20, 2);
    Put(out, 0, 2);
    Put(out, 0, 2);
    Put(out, 0, 2);
    Put(out, 33, 2);
    Put(out, entry.crc, 4);
    Put(out, entry.text.size(), 4);
    Put(out, entry.text.size(), 4);
    Put(out, entry.name.size(), 2);
    Put(out, 0, 2);
    out += entry.name;
    out += entry.text;
  }
  const auto start = out.size();
  for (const auto& entry : entries) {
    Put(out, 0x02014b50, 4);
    Put(out, 20, 2);
    Put(out, 20, 2);
    Put(out, 0, 2);
    Put(out, 0, 2);
    Put(out, 0, 2);
    Put(out, 33, 2);
    Put(out, entry.crc, 4);
    Put(out, entry.text.size(), 4);
    Put(out, entry.text.size(), 4);
    Put(out, entry.name.size(), 2);
    Put(out, 0, 2);
    Put(out, 0, 2);
    Put(out, 0, 2);
    Put(out, 0, 2);
    Put(out, 0, 4);
    Put(out, entry.offset, 4);
    out += entry.name;
  }
  const auto size = out.size() - start;
  Put(out, 0x06054b50, 4);
  Put(out, 0, 2);
  Put(out, 0, 2);
  Put(out, entries.size(), 2);
  Put(out, entries.size(), 2);
  Put(out, size, 4);
  Put(out, start, 4);
  Put(out, 0, 2);
  return out;
}

std::string Platform() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#elif defined(__OHOS__)
  return "HarmonyOS";
#else
  return "Linux";
#endif
}
}  // namespace

std::string DiagnosticFileName() {
  return "CrossDesk-diagnostics-" +
         TimeText(std::time(nullptr), "%Y%m%d-%H%M%S") + ".zip";
}

ExportResult ExportDiagnostics(const std::filesystem::path& application_logs,
                               const std::filesystem::path& transport_logs,
                               const std::filesystem::path& destination,
                               const std::string& version, std::time_t now) {
  std::vector<Entry> entries;
  try {
    for (const auto& [directory, name] :
         {std::pair{application_logs, std::string("crossdesk.log")},
          std::pair{transport_logs, std::string("minirtc.log")}}) {
      const auto path = directory / name;
      std::error_code error;
      const auto status = std::filesystem::symlink_status(path, error);
      if (error == std::errc::no_such_file_or_directory ||
          (!error && status.type() == std::filesystem::file_type::not_found))
        continue;
      if (error || !std::filesystem::is_regular_file(status))
        return {ExportError::read_failed};
      std::ifstream input(path, std::ios::binary | std::ios::ate);
      if (!input || input.tellg() < 0) return {ExportError::read_failed};
      const auto size = static_cast<size_t>(input.tellg());
      // Both rotating loggers normally cap each active file at 5 MiB. Keep
      // the complete archive below GitHub's attachment limit without silently
      // truncating an unexpectedly large log.
      if (size > kMaxSource) return {ExportError::too_large};
      input.seekg(0);
      std::string data(size, '\0');
      input.read(data.data(), static_cast<std::streamsize>(size));
      if (input.bad() || static_cast<size_t>(input.gcount()) != size)
        return {ExportError::read_failed};
      if (!data.empty()) entries.push_back({name, std::move(data)});
    }
  } catch (...) {
    return {ExportError::read_failed};
  }
  if (entries.empty()) return {ExportError::no_logs};
  std::string info =
      "CrossDesk diagnostics\nVersion: " + version +
      "\nPlatform: " + Platform() +
      "\nProcess bits: " + std::to_string(sizeof(void*) * 8) +
      "\nExported UTC: " + TimeText(now, "%Y-%m-%d %H:%M:%S", true) +
      "\nActive log files are included as-is, without filtering or redaction.\n"
      "Configuration files and screen previews are not included.\n"
      "Review the logs before sharing. GitHub issue attachments are public.\n";
  entries.insert(entries.begin(), {"environment.txt", std::move(info)});
  try {
    private_file::Write(destination, Zip(entries));
  } catch (...) {
    return {ExportError::write_failed};
  }
  return {};
}
}  // namespace crossdesk::feedback
