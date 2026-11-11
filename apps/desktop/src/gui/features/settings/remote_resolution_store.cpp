#include "remote_resolution_store.h"

#include <fstream>

#include <display_resolution_preferences.h>

#include "common/private_file.h"

namespace crossdesk {
namespace {
constexpr std::uintmax_t kMaxStoreBytes = 8 * 1024 * 1024;
}

bool RemoteResolutionStore::Load(const std::filesystem::path &path) {
  std::lock_guard lock(mutex_);
  path_ = path;
  devices_.clear();
  try {
    if (!std::filesystem::exists(path_)) return true;
    if (std::filesystem::file_size(path_) > kMaxStoreBytes) return false;
    std::ifstream input(path_, std::ios::binary);
    const auto json = nlohmann::json::parse(input);
    if (json.at("version") != 1 || !json.at("devices").is_object())
      return false;
    for (const auto &[device, values] : json.at("devices").items()) {
      std::vector<DisplayResolutionPreference> preferences;
      if (!device.empty() && ReadDisplayResolutionPreferences(values, preferences))
        devices_.emplace(device, std::move(preferences));
    }
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

std::vector<DisplayResolutionPreference>
RemoteResolutionStore::Get(const std::string &device) const {
  std::lock_guard lock(mutex_);
  const auto found = devices_.find(device);
  return found == devices_.end() ? std::vector<DisplayResolutionPreference>{}
                                : found->second;
}

bool RemoteResolutionStore::Remember(
    const std::string &device, const DisplayResolutionPreference &preference) {
  if (device.empty() || !ValidDisplayResolutionPreference(preference))
    return false;
  std::lock_guard lock(mutex_);
  if (path_.empty()) return false;
  auto updated = devices_;
  auto &preferences = updated[device];
  const auto found = std::find_if(preferences.begin(), preferences.end(),
      [&](const auto &saved) { return saved.display_key == preference.display_key; });
  if (found != preferences.end()) {
    if (found->resolution == preference.resolution) return true;
    *found = preference;
  } else {
    if (preferences.size() >= kMaxResolutionDisplays)
      preferences.erase(preferences.begin());
    preferences.push_back(preference);
  }
  try {
    nlohmann::json json{{"version", 1}, {"devices", nlohmann::json::object()}};
    for (const auto &[id, saved] : updated)
      json["devices"][id] = DisplayResolutionPreferencesJson(saved);
    const auto text = json.dump(2);
    if (text.size() > kMaxStoreBytes) return false;
    private_file::EnsureDirectory(path_.parent_path());
    private_file::Write(path_, text);
    devices_ = std::move(updated);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

} // namespace crossdesk
