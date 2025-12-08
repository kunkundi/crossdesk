#include "config_center.h"

#include "autostart.h"
#include "rd_log.h"

namespace crossdesk {

ConfigCenter::ConfigCenter(const std::string& config_path,
                           const std::string& cert_file_path)
    : config_path_(config_path),
      cert_file_path_(cert_file_path),
      cert_file_path_default_(cert_file_path) {
  ini_.SetUnicode(true);
  Load();
}

ConfigCenter::~ConfigCenter() {}

int ConfigCenter::Load() {
  SI_Error rc = ini_.LoadFile(config_path_.c_str());
  if (rc < 0) {
    Save();
    return -1;
  }

  language_ = static_cast<LANGUAGE>(
      ini_.GetLongValue(section_, "language", static_cast<long>(language_)));

  video_quality_ = static_cast<VIDEO_QUALITY>(ini_.GetLongValue(
      section_, "video_quality", static_cast<long>(video_quality_)));

  video_frame_rate_ = static_cast<VIDEO_FRAME_RATE>(ini_.GetLongValue(
      section_, "video_frame_rate", static_cast<long>(video_frame_rate_)));

  video_encode_format_ = static_cast<VIDEO_ENCODE_FORMAT>(
      ini_.GetLongValue(section_, "video_encode_format",
                        static_cast<long>(video_encode_format_)));

  hardware_video_codec_ = ini_.GetBoolValue(section_, "hardware_video_codec",
                                            hardware_video_codec_);

  enable_turn_ = ini_.GetBoolValue(section_, "enable_turn", enable_turn_);
  enable_srtp_ = ini_.GetBoolValue(section_, "enable_srtp", enable_srtp_);
  enable_self_hosted_ =
      ini_.GetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  const char* signal_server_host_value =
      ini_.GetValue(section_, "signal_server_host", nullptr);
  if (signal_server_host_value != nullptr &&
      strlen(signal_server_host_value) > 0) {
    signal_server_host_ = signal_server_host_value;
  } else {
    signal_server_host_ = "";
  }
  const char* signal_server_port_value =
      ini_.GetValue(section_, "signal_server_port", nullptr);
  if (signal_server_port_value != nullptr &&
      strlen(signal_server_port_value) > 0) {
    signal_server_port_ =
        static_cast<int>(ini_.GetLongValue(section_, "signal_server_port", 0));
  } else {
    signal_server_port_ = 0;
  }
  const char* coturn_server_port_value =
      ini_.GetValue(section_, "coturn_server_port", nullptr);
  if (coturn_server_port_value != nullptr &&
      strlen(coturn_server_port_value) > 0) {
    coturn_server_port_ =
        static_cast<int>(ini_.GetLongValue(section_, "coturn_server_port", 0));
  } else {
    coturn_server_port_ = 0;
  }
  const char* cert_file_path_value =
      ini_.GetValue(section_, "cert_file_path", nullptr);
  if (cert_file_path_value != nullptr && strlen(cert_file_path_value) > 0) {
    cert_file_path_ = cert_file_path_value;
  } else {
    cert_file_path_ = "";
  }

  enable_autostart_ =
      ini_.GetBoolValue(section_, "enable_autostart", enable_autostart_);
  enable_daemon_ = ini_.GetBoolValue(section_, "enable_daemon", enable_daemon_);
  enable_minimize_to_tray_ = ini_.GetBoolValue(
      section_, "enable_minimize_to_tray", enable_minimize_to_tray_);

  return 0;
}

int ConfigCenter::Save() {
  ini_.SetLongValue(section_, "language", static_cast<long>(language_));
  ini_.SetLongValue(section_, "video_quality",
                    static_cast<long>(video_quality_));
  ini_.SetLongValue(section_, "video_frame_rate",
                    static_cast<long>(video_frame_rate_));
  ini_.SetLongValue(section_, "video_encode_format",
                    static_cast<long>(video_encode_format_));
  ini_.SetBoolValue(section_, "hardware_video_codec", hardware_video_codec_);
  ini_.SetBoolValue(section_, "enable_turn", enable_turn_);
  ini_.SetBoolValue(section_, "enable_srtp", enable_srtp_);
  ini_.SetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  // only save when self hosted
  if (enable_self_hosted_) {
    ini_.SetValue(section_, "signal_server_host", signal_server_host_.c_str());
    ini_.SetLongValue(section_, "signal_server_port",
                      static_cast<long>(signal_server_port_));
    ini_.SetLongValue(section_, "coturn_server_port",
                      static_cast<long>(coturn_server_port_));
    ini_.SetValue(section_, "cert_file_path", cert_file_path_.c_str());
  }

  ini_.SetBoolValue(section_, "enable_autostart", enable_autostart_);
  ini_.SetBoolValue(section_, "enable_daemon", enable_daemon_);
  ini_.SetBoolValue(section_, "enable_minimize_to_tray",
                    enable_minimize_to_tray_);

  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }

  return 0;
}

// setters

int ConfigCenter::SetLanguage(LANGUAGE language) {
  language_ = language;
  ini_.SetLongValue(section_, "language", static_cast<long>(language_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetVideoQuality(VIDEO_QUALITY video_quality) {
  video_quality_ = video_quality;
  ini_.SetLongValue(section_, "video_quality",
                    static_cast<long>(video_quality_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetVideoFrameRate(VIDEO_FRAME_RATE video_frame_rate) {
  video_frame_rate_ = video_frame_rate;
  ini_.SetLongValue(section_, "video_frame_rate",
                    static_cast<long>(video_frame_rate_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetVideoEncodeFormat(
    VIDEO_ENCODE_FORMAT video_encode_format) {
  video_encode_format_ = video_encode_format;
  ini_.SetLongValue(section_, "video_encode_format",
                    static_cast<long>(video_encode_format_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetHardwareVideoCodec(bool hardware_video_codec) {
  hardware_video_codec_ = hardware_video_codec;
  ini_.SetBoolValue(section_, "hardware_video_codec", hardware_video_codec_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetTurn(bool enable_turn) {
  enable_turn_ = enable_turn;
  ini_.SetBoolValue(section_, "enable_turn", enable_turn_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetSrtp(bool enable_srtp) {
  enable_srtp_ = enable_srtp;
  ini_.SetBoolValue(section_, "enable_srtp", enable_srtp_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetServerHost(const std::string& signal_server_host) {
  signal_server_host_ = signal_server_host;
  ini_.SetValue(section_, "signal_server_host", signal_server_host_.c_str());
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetServerPort(int signal_server_port) {
  signal_server_port_ = signal_server_port;
  ini_.SetLongValue(section_, "signal_server_port",
                    static_cast<long>(signal_server_port_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetCoturnServerPort(int coturn_server_port) {
  coturn_server_port_ = coturn_server_port;
  ini_.SetLongValue(section_, "coturn_server_port",
                    static_cast<long>(coturn_server_port_));
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetCertFilePath(const std::string& cert_file_path) {
  cert_file_path_ = cert_file_path;
  ini_.SetValue(section_, "cert_file_path", cert_file_path_.c_str());
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetSelfHosted(bool enable_self_hosted) {
  enable_self_hosted_ = enable_self_hosted;
  ini_.SetBoolValue(section_, "enable_self_hosted", enable_self_hosted_);

  // load from config if self hosted is enabled
  if (enable_self_hosted_) {
    const char* signal_server_host_value =
        ini_.GetValue(section_, "signal_server_host", nullptr);
    if (signal_server_host_value != nullptr &&
        strlen(signal_server_host_value) > 0) {
      signal_server_host_ = signal_server_host_value;
    }
    const char* signal_server_port_value =
        ini_.GetValue(section_, "signal_server_port", nullptr);
    if (signal_server_port_value != nullptr &&
        strlen(signal_server_port_value) > 0) {
      signal_server_port_ = static_cast<int>(
          ini_.GetLongValue(section_, "signal_server_port", 0));
    }
    const char* coturn_server_port_value =
        ini_.GetValue(section_, "coturn_server_port", nullptr);
    if (coturn_server_port_value != nullptr &&
        strlen(coturn_server_port_value) > 0) {
      coturn_server_port_ = static_cast<int>(
          ini_.GetLongValue(section_, "coturn_server_port", 0));
    }
    const char* cert_file_path_value =
        ini_.GetValue(section_, "cert_file_path", nullptr);
    if (cert_file_path_value != nullptr && strlen(cert_file_path_value) > 0) {
      cert_file_path_ = cert_file_path_value;
    }

    ini_.SetValue(section_, "signal_server_host", signal_server_host_.c_str());
    ini_.SetLongValue(section_, "signal_server_port",
                      static_cast<long>(signal_server_port_));
    ini_.SetLongValue(section_, "coturn_server_port",
                      static_cast<long>(coturn_server_port_));
    ini_.SetValue(section_, "cert_file_path", cert_file_path_.c_str());
  }

  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetMinimizeToTray(bool enable_minimize_to_tray) {
  enable_minimize_to_tray_ = enable_minimize_to_tray;
  ini_.SetBoolValue(section_, "enable_minimize_to_tray",
                    enable_minimize_to_tray_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }
  return 0;
}

int ConfigCenter::SetAutostart(bool enable_autostart) {
  enable_autostart_ = enable_autostart;
  bool success = false;
  if (enable_autostart) {
    success = EnableAutostart("CrossDesk");
  } else {
    success = DisableAutostart("CrossDesk");
  }

  ini_.SetBoolValue(section_, "enable_autostart", enable_autostart_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }

  if (!success) {
    LOG_ERROR("SetAutostart failed");
    return -1;
  }

  return 0;
}

int ConfigCenter::SetDaemon(bool enable_daemon) {
  enable_daemon_ = enable_daemon;

  ini_.SetBoolValue(section_, "enable_daemon", enable_daemon_);
  SI_Error rc = ini_.SaveFile(config_path_.c_str());
  if (rc < 0) {
    return -1;
  }

  return 0;
}

// getters

ConfigCenter::LANGUAGE ConfigCenter::GetLanguage() const { return language_; }

ConfigCenter::VIDEO_QUALITY ConfigCenter::GetVideoQuality() const {
  return video_quality_;
}

ConfigCenter::VIDEO_FRAME_RATE ConfigCenter::GetVideoFrameRate() const {
  return video_frame_rate_;
}

ConfigCenter::VIDEO_ENCODE_FORMAT ConfigCenter::GetVideoEncodeFormat() const {
  return video_encode_format_;
}

bool ConfigCenter::IsHardwareVideoCodec() const {
  return hardware_video_codec_;
}

bool ConfigCenter::IsEnableTurn() const { return enable_turn_; }

bool ConfigCenter::IsEnableSrtp() const { return enable_srtp_; }

std::string ConfigCenter::GetSignalServerHost() const {
  return signal_server_host_;
}

int ConfigCenter::GetSignalServerPort() const { return signal_server_port_; }

int ConfigCenter::GetCoturnServerPort() const { return coturn_server_port_; }

std::string ConfigCenter::GetCertFilePath() const { return cert_file_path_; }

std::string ConfigCenter::GetDefaultServerHost() const {
  return signal_server_host_default_;
}

int ConfigCenter::GetDefaultSignalServerPort() const {
  return server_port_default_;
}

int ConfigCenter::GetDefaultCoturnServerPort() const {
  return coturn_server_port_default_;
}

std::string ConfigCenter::GetDefaultCertFilePath() const {
  return cert_file_path_default_;
}

bool ConfigCenter::IsSelfHosted() const { return enable_self_hosted_; }

bool ConfigCenter::IsMinimizeToTray() const { return enable_minimize_to_tray_; }

bool ConfigCenter::IsEnableAutostart() const { return enable_autostart_; }

bool ConfigCenter::IsEnableDaemon() const { return enable_daemon_; }
}  // namespace crossdesk