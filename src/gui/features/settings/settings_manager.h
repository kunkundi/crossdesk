#ifndef CROSSDESK_GUI_SETTINGS_MANAGER_H_
#define CROSSDESK_GUI_SETTINGS_MANAGER_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "thumbnail.h"

namespace crossdesk {

class GuiRuntime;

// Owns persistent GUI settings and recent-connection aliases. GuiRuntime keeps
// only the runtime/UI state that consumes these values.
class SettingsManager {
public:
  explicit SettingsManager(GuiRuntime &owner);

  int Save();
  int Load();

  int LoadRecentConnectionAliases();
  int SaveRecentConnectionAliases() const;
  std::string RecentConnectionDisplayName(
      const Thumbnail::RecentConnection &connection) const;
  void
  BeginEditRecentConnectionAlias(const Thumbnail::RecentConnection &connection);
  void SetRecentConnectionAlias(const std::string &remote_id,
                                const std::string &alias);
  void EraseRecentConnectionAlias(const std::string &remote_id);

  // Loads the cached self-hosted identity into the owner's active connection
  // fields. Returns true only when a non-empty identity was restored.
  bool LoadCachedSelfHostedIdentity();
  void PersistSelfHostedIdentity(const char *client_id);

private:
  struct CacheV1 {
    char client_id_with_password[17];
    int language;
    int video_quality;
    int video_frame_rate;
    int video_encode_format;
    bool enable_hardware_video_codec;
    bool enable_turn;
    bool enable_srtp;
    unsigned char key[16];
    unsigned char iv[16];
  };

  struct CacheV2 {
    char client_id_with_password[17];
    int language;
    int video_quality;
    int video_frame_rate;
    int video_encode_format;
    bool enable_hardware_video_codec;
    bool enable_turn;
    bool enable_srtp;
    unsigned char key[16];
    unsigned char iv[16];
    char self_hosted_id[17];
  };

  int SaveLocked();
  bool ReadV2Locked();

  GuiRuntime &owner_;
  CacheV1 cache_v1_{};
  CacheV2 cache_v2_{};
  mutable std::mutex cache_mutex_;
  std::unordered_map<std::string, std::string> recent_connection_aliases_;
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_SETTINGS_MANAGER_H_
