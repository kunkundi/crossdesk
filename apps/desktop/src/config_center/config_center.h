/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONFIG_CENTER_H_
#define _CONFIG_CENTER_H_

#include <minirtc.h>

#include <atomic>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <string>


namespace crossdesk {

class ConfigCenter {
 public:
  enum class LANGUAGE {
    CHINESE = 0, ENGLISH = 1, RUSSIAN = 2, TRADITIONAL_CHINESE = 3,
    JAPANESE = 4, KOREAN = 5, GERMAN = 6, FRENCH = 7, SPANISH = 8,
    BRAZILIAN_PORTUGUESE = 9, SYSTEM = 10
  };
  enum class CONNECTION_MODE { DEVICE_ID = 0, DIRECT_IP = 1 };
  using VIDEO_ENCODE_FORMAT = MiniRtcVideoCodec;
  enum class TURN_MODE {
    DISABLED = 0,
    AUTO_UDP_TCP = 1,
    FORCE_UDP = 2,
    FORCE_TCP = 3
  };

 public:
  explicit ConfigCenter(const std::string& config_path = "config.ini");
  ~ConfigCenter();

  struct StreamWindowSize {
    int width;
    int height;
    bool operator==(const StreamWindowSize& other) const {
      return width == other.width && height == other.height;
    }
  };

  // write config
  int SetLanguage(LANGUAGE language);
  int SetConnectionMode(CONNECTION_MODE mode);
  int SetIpDirectEnabled(bool enabled);
  int SetDesktopRealtimeRateControl(bool enabled);
  int SetVideoEncodeFormat(VIDEO_ENCODE_FORMAT video_encode_format);
  int SetTurnMode(TURN_MODE turn_mode);
  int SetForceRelay(bool force_relay);
  int SetServerHost(const std::string& signal_server_host);
  int SetServerPort(int signal_server_port);
  int SetServerEndpoint(const std::string& address, int port);
  int SetSelfHosted(bool enable_self_hosted);
  int SetAutostart(bool enable_autostart);
  int SetDaemon(bool enable_daemon);
  int SetPrivacyScreen(bool enable_privacy_screen);
  int SetPrivacyLocalUnlock(bool allowed);
  int SetPrivacyBlackScreen(bool enabled);
  int SetLockOnDisconnect(bool enabled);
  int SetTrustedDevicesOnly(bool enabled);
  int SetSaveRemotePreviews(bool enabled);
  int SetPauseVideoWhenMinimized(bool enabled);
  int SetInputSensitivity(const std::string& device, int mouse_percent, int wheel_percent);
  int SetStreamWindowSize(const std::string& device, StreamWindowSize size);
  int SetPortableServicePromptSuppressed(bool suppressed);
  int SetLastPresentedRelease(const std::string& version);
  int SetFileTransferSavePath(const std::string& path);

  // read config

  LANGUAGE GetLanguage() const;
  CONNECTION_MODE GetConnectionMode() const;
  bool IsIpDirectEnabled() const;
  VIDEO_ENCODE_FORMAT GetVideoEncodeFormat() const;
  bool IsDesktopRealtimeRateControlEnabled() const { return desktop_realtime_rate_control_.load(); }
  MiniRtcDesktopEncodingMode GetDesktopEncodingMode() const {
    return IsDesktopRealtimeRateControlEnabled() ? MiniRtcDesktopEncodingMode::LowLoad
                                                : MiniRtcDesktopEncodingMode::EncoderDefault;
  }
  TURN_MODE GetTurnMode() const;
  int GetDirectStartBitrateBps() const;
  int GetRelayStartBitrateBps() const;
  bool IsReceiverPaddingProbingDisabled() const;
  bool IsForceRelay() const;
  bool IsEnableSrtp() const;
  std::string GetSignalServerHost() const;
  int GetSignalServerPort() const;
  std::string GetDefaultServerHost() const;
  int GetDefaultSignalServerPort() const;
  bool IsSelfHosted() const;
  bool IsEnableAutostart() const;
  bool IsEnableDaemon() const;
  bool IsEnablePrivacyScreen() const;
  bool IsPrivacyLocalUnlockAllowed() const;
  bool IsPrivacyBlackScreen() const { return privacy_black_screen_.load(); }
  bool IsLockOnDisconnect() const;
  bool IsTrustedDevicesOnly() const;
  bool IsSaveRemotePreviews() const;
  bool IsPauseVideoWhenMinimized() const;
  int GetMouseSensitivityPercent() const { return mouse_sensitivity_percent_.load(); }
  int GetWheelSensitivityPercent(const std::string& device) const;
  std::optional<StreamWindowSize> GetStreamWindowSize(const std::string& device) const;
  bool IsPortableServicePromptSuppressed() const;
  std::string GetLastPresentedRelease() const;
  std::string GetFileTransferSavePath() const;

  int Load();
  int Save();
  // Only connection/admission preferences cross into the SYSTEM host profile.
  std::string ExportSharedHostConfig() const;
  int ApplySharedHostConfig(const std::string& contents);

 private:
  int CommitIni(bool warn_only = false);
  int StoreValues(std::initializer_list<std::pair<const char*, std::string>> values);
  mutable std::mutex file_path_mutex_;
  std::string config_path_;
  struct IniStorage;
  std::unique_ptr<IniStorage> ini_;
  const char* section_ = "Settings";

  LANGUAGE language_ = LANGUAGE::SYSTEM;
  CONNECTION_MODE connection_mode_ = CONNECTION_MODE::DEVICE_ID;
  bool enable_ip_direct_ = true;
  std::atomic<bool> desktop_realtime_rate_control_{true};
  VIDEO_ENCODE_FORMAT video_encode_format_ = VIDEO_ENCODE_FORMAT::H264;
  TURN_MODE turn_mode_ = TURN_MODE::AUTO_UDP_TCP;
  static constexpr int kDefaultDirectStartBitrateBps = 2'500'000;
  static constexpr int kDefaultRelayStartBitrateBps = 1'500'000;
  int direct_start_bitrate_bps_ = kDefaultDirectStartBitrateBps;
  int relay_start_bitrate_bps_ = kDefaultRelayStartBitrateBps;
  static constexpr bool kDefaultDisableReceiverPaddingProbing = true;
  bool disable_receiver_padding_probing_ = kDefaultDisableReceiverPaddingProbing;
  std::string signal_server_host_ = "";
  std::string signal_server_host_default_ = "api.crossdesk.cn";
  int signal_server_port_ = 0;
  int server_port_default_ = 9099;
  bool enable_self_hosted_ = false;
  bool enable_autostart_ = false;
  bool enable_daemon_ = false;
  std::atomic<bool> enable_privacy_screen_{true};
  std::atomic<bool> privacy_local_unlock_allowed_{true};
  std::atomic<bool> privacy_black_screen_{false};
  std::atomic<bool> lock_on_disconnect_{true};
  std::atomic<bool> trusted_devices_only_{false};
  std::atomic<bool> save_remote_previews_{true};
  std::atomic<bool> pause_video_when_minimized_{true};
  std::atomic<int> mouse_sensitivity_percent_{100};
  int legacy_wheel_sensitivity_percent_ = 100;
  std::map<std::string, int> device_wheel_sensitivity_;
  bool portable_service_prompt_suppressed_ = false;
  std::string last_presented_release_;
  std::string file_transfer_save_path_ = "";
};
}  // namespace crossdesk
#endif
