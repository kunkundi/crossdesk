#ifndef CROSSDESK_GUI_SETTINGS_MANAGER_H_
#define CROSSDESK_GUI_SETTINGS_MANAGER_H_

#include <cstdint>
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
  // Restores the public-server identity kept in the in-memory cache fields.
  // This must run when switching away from a self-hosted server so signal
  // callbacks are matched against the identity used by the replacement peer.
  bool ActivateCachedPublicIdentity();
  void PersistSelfHostedIdentity(const char *client_id);

  // Password rotation is deliberately persisted in two phases. The active
  // credential remains usable while the pending credential records the value
  // that may already have been committed by the server. On the next launch the
  // caller can try the pending credential first and safely fall back to the
  // active credential when the request never reached the server.
  bool StagePendingPasswordChange(const std::string &identity,
                                  bool self_hosted,
                                  const std::string &server_host,
                                  int server_port,
                                  const std::string &request_id);
  std::string PendingPasswordChangeIdentity(
      bool self_hosted, const std::string &server_host,
      int server_port) const;
  bool PromotePendingPasswordChange();
  bool ClearPendingPasswordChange();

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

  struct CacheV3 {
    uint32_t magic;
    uint32_t version;
    CacheV2 base;
    char pending_identity[17];
    char pending_server_host[256];
    int pending_server_port;
    bool pending_self_hosted;
    char pending_request_id[64];
    uint32_t checksum;
  };

  int SaveLocked();
  bool ReadV3Locked();
  bool ReadV2Locked();
  void ActivateIdentity(const char *identity, bool self_hosted);

  GuiRuntime &owner_;
  CacheV1 cache_v1_{};
  CacheV2 cache_v2_{};
  CacheV3 cache_v3_{};
  mutable std::mutex cache_mutex_;
  std::unordered_map<std::string, std::string> recent_connection_aliases_;
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_SETTINGS_MANAGER_H_
