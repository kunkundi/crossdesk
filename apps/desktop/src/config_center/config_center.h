/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONFIG_CENTER_H_
#define _CONFIG_CENTER_H_

#include <atomic>
#include <initializer_list>
#include <mutex>
#include <utility>
#include <string>

#include "SimpleIni.h"
#include "screen_capturer/screen_capture_method.h"

namespace crossdesk {

class ConfigCenter {
 public:
  enum class LANGUAGE { CHINESE = 0, ENGLISH = 1, RUSSIAN = 2 };
  enum class VIDEO_ENCODE_FORMAT { H264 = 0, AV1 = 1 };
  enum class TURN_MODE {
    DISABLED = 0,
    AUTO_UDP_TCP = 1,
    FORCE_UDP = 2,
    FORCE_TCP = 3
  };

 public:
  explicit ConfigCenter(const std::string& config_path = "config.ini");
  ~ConfigCenter();

  // write config
  int SetLanguage(LANGUAGE language);
  int SetScreenCaptureMethod(ScreenCaptureMethod method);
  int SetVideoEncodeFormat(VIDEO_ENCODE_FORMAT video_encode_format);
  int SetHardwareVideoCodec(bool hardware_video_codec);
  int SetTurnMode(TURN_MODE turn_mode);
  int SetTurn(bool enable_turn, bool force_relay);
  int SetServerHost(const std::string& signal_server_host);
  int SetServerPort(int signal_server_port);
  int SetSelfHosted(bool enable_self_hosted);
  int SetAutostart(bool enable_autostart);
  int SetDaemon(bool enable_daemon);
  int SetPrivacyScreen(bool enable_privacy_screen);
  int SetPortableServicePromptSuppressed(bool suppressed);
  int SetFileTransferSavePath(const std::string& path);

  // read config

  LANGUAGE GetLanguage() const;
  ScreenCaptureMethod GetScreenCaptureMethod() const;
  VIDEO_ENCODE_FORMAT GetVideoEncodeFormat() const;
  // Whether this build includes a hardware video codec backend.
  static bool IsHardwareVideoCodecAvailable();
  bool IsHardwareVideoCodec() const;
  TURN_MODE GetTurnMode() const;
  bool IsEnableTurn() const;
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
  bool IsPortableServicePromptSuppressed() const;
  std::string GetFileTransferSavePath() const;

  int Load();
  int Save();

 private:
  int CommitIni();
  int StoreValues(std::initializer_list<std::pair<const char*, std::string>> values);
  mutable std::mutex file_path_mutex_;
  std::string config_path_;
  CSimpleIniA ini_;
  const char* section_ = "Settings";

  LANGUAGE language_ = LANGUAGE::CHINESE;
  ScreenCaptureMethod screen_capture_method_ = ScreenCaptureMethod::Auto;
  VIDEO_ENCODE_FORMAT video_encode_format_ = VIDEO_ENCODE_FORMAT::H264;
  bool hardware_video_codec_ = false;
  TURN_MODE turn_mode_ = TURN_MODE::AUTO_UDP_TCP;
  std::string signal_server_host_ = "";
  std::string signal_server_host_default_ = "api.crossdesk.cn";
  int signal_server_port_ = 0;
  int server_port_default_ = 9099;
  bool enable_self_hosted_ = false;
  bool enable_autostart_ = false;
  bool enable_daemon_ = false;
  std::atomic<bool> enable_privacy_screen_{true};
  bool portable_service_prompt_suppressed_ = false;
  std::string file_transfer_save_path_ = "";
};
}  // namespace crossdesk
#endif
