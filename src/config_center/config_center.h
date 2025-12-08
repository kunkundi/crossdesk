/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONFIG_CENTER_H_
#define _CONFIG_CENTER_H_

#include <string>

#include "SimpleIni.h"

namespace crossdesk {

class ConfigCenter {
 public:
  enum class LANGUAGE { CHINESE = 0, ENGLISH = 1 };
  enum class VIDEO_QUALITY { LOW = 0, MEDIUM = 1, HIGH = 2 };
  enum class VIDEO_FRAME_RATE { FPS_30 = 0, FPS_60 = 1 };
  enum class VIDEO_ENCODE_FORMAT { H264 = 0, AV1 = 1 };

 public:
  explicit ConfigCenter(
      const std::string& config_path = "config.ini",
      const std::string& cert_file_path = "crossdesk.cn_root.crt");
  ~ConfigCenter();

  // write config
  int SetLanguage(LANGUAGE language);
  int SetVideoQuality(VIDEO_QUALITY video_quality);
  int SetVideoFrameRate(VIDEO_FRAME_RATE video_frame_rate);
  int SetVideoEncodeFormat(VIDEO_ENCODE_FORMAT video_encode_format);
  int SetHardwareVideoCodec(bool hardware_video_codec);
  int SetTurn(bool enable_turn);
  int SetSrtp(bool enable_srtp);
  int SetServerHost(const std::string& signal_server_host);
  int SetServerPort(int signal_server_port);
  int SetCoturnServerPort(int coturn_server_port);
  int SetCertFilePath(const std::string& cert_file_path);
  int SetSelfHosted(bool enable_self_hosted);
  int SetMinimizeToTray(bool enable_minimize_to_tray);
  int SetAutostart(bool enable_autostart);
  int SetDaemon(bool enable_daemon);

  // read config

  LANGUAGE GetLanguage() const;
  VIDEO_QUALITY GetVideoQuality() const;
  VIDEO_FRAME_RATE GetVideoFrameRate() const;
  VIDEO_ENCODE_FORMAT GetVideoEncodeFormat() const;
  bool IsHardwareVideoCodec() const;
  bool IsEnableTurn() const;
  bool IsEnableSrtp() const;
  std::string GetSignalServerHost() const;
  int GetSignalServerPort() const;
  int GetCoturnServerPort() const;
  std::string GetCertFilePath() const;
  std::string GetDefaultServerHost() const;
  int GetDefaultSignalServerPort() const;
  int GetDefaultCoturnServerPort() const;
  std::string GetDefaultCertFilePath() const;
  bool IsSelfHosted() const;
  bool IsMinimizeToTray() const;
  bool IsEnableAutostart() const;
  bool IsEnableDaemon() const;

  int Load();
  int Save();

 private:
  std::string config_path_;
  CSimpleIniA ini_;
  const char* section_ = "Settings";

  LANGUAGE language_ = LANGUAGE::CHINESE;
  VIDEO_QUALITY video_quality_ = VIDEO_QUALITY::MEDIUM;
  VIDEO_FRAME_RATE video_frame_rate_ = VIDEO_FRAME_RATE::FPS_60;
  VIDEO_ENCODE_FORMAT video_encode_format_ = VIDEO_ENCODE_FORMAT::H264;
  bool hardware_video_codec_ = false;
  bool enable_turn_ = true;
  bool enable_srtp_ = false;
  std::string signal_server_host_ = "";
  std::string signal_server_host_default_ = "api.crossdesk.cn";
  int signal_server_port_ = 0;
  int server_port_default_ = 9099;
  int coturn_server_port_ = 0;
  int coturn_server_port_default_ = 3478;
  std::string cert_file_path_ = "";
  std::string cert_file_path_default_ = "";
  bool enable_self_hosted_ = false;
  bool enable_minimize_to_tray_ = false;
  bool enable_autostart_ = false;
  bool enable_daemon_ = false;
};
}  // namespace crossdesk
#endif