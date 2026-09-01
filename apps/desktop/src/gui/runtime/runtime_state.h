#ifndef _RUNTIME_STATE_H_
#define _RUNTIME_STATE_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stream_names.h>

#include "config_center.h"
#include "path_manager.h"
#include "runtime/device_presence_cache.h"
#include "runtime/remote_session.h"
#include "thumbnail.h"

namespace crossdesk::gui_detail {

struct InfrastructureState {
  std::unique_ptr<ConfigCenter> config_center_;
  ConfigCenter::LANGUAGE localization_language_ =
      ConfigCenter::LANGUAGE::CHINESE;
  std::unique_ptr<PathManager> path_manager_;
  std::string exec_log_path_;
  std::string dll_log_path_;
  std::string cache_path_;
  int localization_language_index_ = -1;
  int localization_language_index_last_ = -1;
  bool modules_inited_ = false;
  unsigned char aes128_key_[16]{};
  unsigned char aes128_iv_[16]{};
};

struct RecentConnectionsState {
  std::shared_ptr<Thumbnail> thumbnail_;
  std::vector<std::pair<std::string, Thumbnail::RecentConnection>>
      recent_connections_;
  std::vector<std::string> recent_connection_ids_;
  int recent_connection_image_width_ = 160;
  int recent_connection_image_height_ = 90;
  uint32_t recent_connection_image_save_time_ = 0;
  DevicePresenceCache device_presence_cache_;
  bool need_to_send_recent_connections_ = true;
};

struct PeerState {
  SignalStatus signal_status_ = SignalStatus::SignalClosed;
  std::string signal_status_str_;
  bool signal_connected_ = false;
  PeerPtr *peer_ = nullptr;
  PeerPtr *peer_reserved_ = nullptr;
  std::string video_primary_label_ = "primary_display";
  std::string video_secondary_label_ = "secondary_display";
  std::string audio_label_ = "audio";
  std::string data_label_ = kDataStream;
  std::string mouse_label_ = kMouseStream;
  std::string keyboard_label_ = kKeyboardStream;
  std::string info_label_ = "info";
  std::string control_data_label_ = kControlStream;
  std::string file_label_ = kFileStream;
  std::string file_feedback_label_ = kFileFeedbackStream;
  std::string clipboard_label_ = kClipboardStream;
  Params params_;
};

// OS-specific service and permission state is kept separate from transport
// state so platform code does not appear to be part of the wire contract.
struct PlatformIntegrationState {
#if _WIN32
  std::atomic<bool> pending_windows_service_sas_{false};
  bool local_service_status_received_ = false;
  bool local_service_available_ = false;
  std::string local_interactive_stage_;
  uint32_t last_local_secure_input_block_log_tick_ = 0;
  uint32_t last_windows_service_status_tick_ = 0;
  uint32_t optimistic_windows_secure_desktop_until_tick_ = 0;
#if CROSSDESK_PORTABLE
  enum class PortableServiceInstallState {
    idle,
    installing,
    succeeded,
    failed,
  };
  bool portable_service_prompt_checked_ = false;
  bool show_portable_service_install_window_ = false;
  bool show_portable_service_prompt_suppressed_window_ = false;
  bool portable_service_do_not_remind_ = false;
  bool portable_service_prompt_suppressed_ = false;
  bool portable_service_installed_ = false;
  std::atomic<PortableServiceInstallState> portable_service_install_state_{
      PortableServiceInstallState::idle};
  std::thread portable_service_install_thread_;
#endif
#endif

#ifdef __APPLE__
  bool show_request_permission_window_ = true;
  bool mac_permission_status_initialized_ = false;
  uint32_t mac_permission_last_check_tick_ = 0;
  bool mac_screen_recording_permission_granted_ = false;
  bool mac_accessibility_permission_granted_ = false;
  bool mac_screen_recording_permission_requested_ = false;
  bool mac_accessibility_permission_requested_ = false;
#endif
};

struct UserSettingsState {
  char client_id_[10] = "";
  char client_id_display_[12] = "";
  char client_id_with_password_[17] = "";
  char password_saved_[7] = "";
  char self_hosted_id_[17] = "";
  char self_hosted_user_id_[17] = "";
  char connection_login_identity_[17] = "";
  int language_button_value_ = 0;
  int video_quality_button_value_ = 2;
  int video_frame_rate_button_value_ = 1;
  int video_adaptation_policy_button_value_ = 1;
  int video_encode_format_button_value_ = 0;
  bool enable_hardware_video_codec_ = true;
  bool enable_turn_ = true;
  bool enable_srtp_ = false;
  char signal_server_ip_[256] = "api.crossdesk.cn";
  char signal_server_port_[6] = "9099";
  char coturn_server_port_[6] = "3478";
  bool enable_self_hosted_ = false;
  int language_button_value_last_ = 0;
  int video_quality_button_value_last_ = 0;
  int video_frame_rate_button_value_last_ = 0;
  int video_adaptation_policy_button_value_last_ = 1;
  int video_encode_format_button_value_last_ = 0;
  bool enable_hardware_video_codec_last_ = false;
  bool enable_turn_last_ = true;
  bool enable_srtp_last_ = false;
  bool enable_self_hosted_last_ = false;
  bool enable_autostart_ = false;
  bool enable_autostart_last_ = false;
  bool enable_daemon_ = false;
  bool enable_daemon_last_ = false;
  char file_transfer_save_path_buf_[512] = "";
  std::string file_transfer_save_path_last_;
  char signal_server_ip_self_[256] = "";
  char signal_server_port_self_[6] = "";
  char coturn_server_port_self_[6] = "";
  bool settings_window_pos_reset_ = true;
  bool self_hosted_server_config_window_pos_reset_ = true;
  std::string selected_current_file_path_;
  bool show_file_browser_ = true;
};

struct PasswordChangeState {
  std::mutex password_change_mutex_;
  uint64_t next_password_change_request_id_ = 0;
  bool password_change_pending_ = false;
  bool password_change_result_ready_ = false;
  bool password_change_succeeded_ = false;
  bool password_change_result_uncertain_ = false;
  std::chrono::steady_clock::time_point password_change_requested_at_;
  std::string pending_password_change_request_id_;
  std::string pending_local_password_;
  std::string password_change_error_;
  bool credential_recovery_in_progress_ = false;
  bool credential_recovery_attempt_pending_ = false;
  bool credential_recovery_retry_active_ = false;
  bool credential_recovery_promote_pending_ = false;
  bool credential_recovery_clear_pending_ = false;
};

struct ConnectionState {
  using RemoteSessionMap =
      std::unordered_map<std::string, RemoteSessionPtr>;
  RemoteSessionMap remote_sessions_;
  std::shared_mutex remote_sessions_mutex_;
  std::vector<std::thread> thumbnail_save_threads_;
  std::mutex thumbnail_save_threads_mutex_;

  std::shared_mutex connection_status_mutex_;
  std::unordered_map<std::string, ConnectionStatus> connection_status_;
  std::unordered_map<std::string, std::string> connection_host_names_;
  // Cursor position is sampled asynchronously after remote mouse input has
  // been injected. Remember the input source so the sampled position is not
  // immediately echoed back to that same controller as stale feedback.
  std::mutex remote_pointer_input_mutex_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      last_remote_pointer_input_time_;
  std::string selected_server_remote_id_;
  std::string selected_server_remote_hostname_;
  std::mutex pending_presence_probe_mutex_;
  bool pending_presence_probe_ = false;
  bool pending_presence_result_ready_ = false;
  bool pending_presence_online_ = false;
  std::chrono::steady_clock::time_point pending_presence_probe_started_at_;
  std::string pending_presence_remote_id_;
  std::string pending_presence_password_;
  bool pending_presence_remember_password_ = false;
};

struct RuntimeState : InfrastructureState,
                      RecentConnectionsState,
                      PeerState,
                      PlatformIntegrationState,
                      UserSettingsState,
                      PasswordChangeState,
                      ConnectionState {};

} // namespace crossdesk::gui_detail

#endif
