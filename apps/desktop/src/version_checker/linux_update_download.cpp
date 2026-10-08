#include "linux_update_download.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace crossdesk {

std::string LinuxUpdateDownloadUrl(const nlohmann::json& manifest,
                                   const VersionInfo& latest,
                                   const std::string& architecture,
                                   const std::filesystem::path& executable_dir) {
  const std::string fallback = "https://github.com/kunkundi/crossdesk/releases";
  if ((architecture != "amd64" && architecture != "arm64") ||
      executable_dir.empty()) {
    return fallback;
  }

  std::string package_name = "crossdesk";
  const auto marker = executable_dir / "package-name";
  std::error_code error;
  const bool has_marker = std::filesystem::exists(marker, error);
  if (error) return fallback;
  if (has_marker) {
    std::ifstream file(marker);
    if (!std::getline(file, package_name)) return fallback;
  }
  std::string key = "linux-" + architecture;
  if (package_name == "crossdesk-virtual-desktop-xfce") {
    key += "-virtual-desktop-xfce";
  } else if (package_name != "crossdesk") {
    return fallback;
  }

  try {
    const auto& download = manifest.at("downloads").at(key);
    const auto version = ParseVersionInfoJSON(download.dump());
    // A partial release can leave older downloads in the merged manifest.
    if (!version || version->version != latest.version ||
        IsNewerVersionWithMetadata(version->version, latest.version,
                                   latest.release_date, latest.patch)) {
      return fallback;
    }
    const std::string filename = package_name + "-linux-" + architecture +
                                 "-v" + latest.version + ".deb";
    const std::string url = "https://downloads.crossdesk.cn/" + filename;
    // Verify the variant in the asset name too, not just the manifest key.
    if (download.at("filename") == filename && download.at("url") == url) {
      return url;
    }
  } catch (const nlohmann::json::exception&) {
  }
  return fallback;
}

}  // namespace crossdesk
