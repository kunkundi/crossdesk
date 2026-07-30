#ifndef CROSSDESK_GUI_REMOTE_SESSION_H_
#define CROSSDESK_GUI_REMOTE_SESSION_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "display_info.h"
#include "minirtc.h"

namespace crossdesk::gui_detail {

struct FileTransferState {
  std::atomic<bool> file_sending_ = false;
  std::atomic<uint64_t> file_sent_bytes_ = 0;
  std::atomic<uint64_t> file_total_bytes_ = 0;
  std::atomic<uint32_t> file_send_rate_bps_ = 0;
  std::mutex file_transfer_mutex_;
  std::chrono::steady_clock::time_point file_send_start_time_;
  std::chrono::steady_clock::time_point file_send_last_update_time_;
  uint64_t file_send_last_bytes_ = 0;
  bool file_transfer_window_visible_ = false;
  bool file_transfer_window_hovered_ = false;
  std::atomic<uint32_t> current_file_id_{0};

  struct QueuedFile {
    std::filesystem::path file_path;
    std::string file_label;
    std::string remote_id;
  };
  std::queue<QueuedFile> file_send_queue_;
  std::mutex file_queue_mutex_;

  enum class FileTransferStatus { Queued, Sending, Completed, Failed };

  struct FileTransferInfo {
    std::string file_name;
    std::filesystem::path file_path;
    uint64_t file_size = 0;
    FileTransferStatus status = FileTransferStatus::Queued;
    uint64_t sent_bytes = 0;
    uint32_t file_id = 0;
    uint32_t rate_bps = 0;
  };
  std::vector<FileTransferInfo> file_transfer_list_;
  std::mutex file_transfer_list_mutex_;
};

// Runtime state for one connected or connecting remote endpoint. It groups the
// connection, media, input, window and transfer data that share one lifetime.
struct RemoteSession {
  Params params_;
  PeerPtr* peer_ = nullptr;
  std::string audio_label_ = "control_audio";
  std::string data_label_ = "data";
  std::string mouse_label_ = "mouse";
  std::string keyboard_label_ = "keyboard";
  std::string file_label_ = "file";
  std::string control_data_label_ = "control_data";
  std::string file_feedback_label_ = "file_feedback";
  std::string clipboard_label_ = "clipboard";
  std::string local_id_;
  std::string remote_id_;
  bool exit_ = false;
  bool signal_connected_ = false;
  SignalStatus signal_status_ = SignalStatus::SignalClosed;
  bool connection_established_ = false;
  bool rejoin_ = false;
  bool net_traffic_stats_button_pressed_ = false;
  bool enable_mouse_control_ = true;
  bool mouse_controller_is_started_ = false;
  bool audio_capture_button_pressed_ = true;
  bool control_mouse_ = true;
  bool streaming_ = false;
  bool is_control_bar_in_left_ = true;
  bool control_bar_hovered_ = false;
  bool display_selectable_hovered_ = false;
  bool shortcut_selectable_hovered_ = false;
  bool control_bar_expand_ = true;
  bool reset_control_bar_pos_ = false;
  bool control_window_width_is_changing_ = false;
  bool control_window_height_is_changing_ = false;
  bool p2p_mode_ = true;
  bool remember_password_ = false;
  char remote_password_[7] = "";
  float sub_stream_window_width_ = 1280;
  float sub_stream_window_height_ = 720;
  float control_window_min_width_ = 20;
  float control_window_max_width_ = 300;
  float control_window_min_height_ = 38;
  float control_window_max_height_ = 180;
  float control_window_width_ = 300;
  float control_window_height_ = 38;
  float control_bar_pos_x_ = 0;
  float control_bar_pos_y_ = 30;
  float mouse_diff_control_bar_pos_x_ = 0;
  float mouse_diff_control_bar_pos_y_ = 0;
  double control_bar_button_pressed_time_ = 0;
  double net_traffic_stats_button_pressed_time_ = 0;

  // Written by the decode callback thread and consumed by the UI thread.
  std::mutex video_frame_mutex_;
  std::shared_ptr<std::vector<unsigned char>> front_frame_;
  std::shared_ptr<std::vector<unsigned char>> back_frame_;
  bool render_rect_dirty_ = false;
  bool stream_cleanup_pending_ = false;
  float mouse_pos_x_ = 0;
  float mouse_pos_y_ = 0;
  float mouse_pos_x_last_ = 0;
  float mouse_pos_y_last_ = 0;
  int texture_width_ = 1280;
  int texture_height_ = 720;
  int video_width_ = 0;
  int video_height_ = 0;
  int video_width_last_ = 0;
  int video_height_last_ = 0;
  int selected_display_ = 0;
  size_t video_size_ = 0;
  uint64_t video_frame_sequence_ = 0;
  bool tab_selected_ = false;
  bool tab_opened_ = true;
  std::optional<float> pos_x_before_docked_;
  std::optional<float> pos_y_before_docked_;
  float render_window_x_ = 0;
  float render_window_y_ = 0;
  float render_window_width_ = 0;
  float render_window_height_ = 0;
  std::string fullscreen_button_label_ = "Fullscreen";
  std::string net_traffic_stats_button_label_ = "Show Net Traffic Stats";
  std::string mouse_control_button_label_ = "Mouse Control";
  std::string audio_capture_button_label_ = "Audio Capture";
  std::string remote_host_name_;
  bool remote_service_status_received_ = false;
  bool remote_service_available_ = false;
  std::string remote_interactive_stage_;
  std::vector<DisplayInfo> display_info_list_;
  // Shared by minirtc callbacks, Slint rendering and SDL audio callbacks.
  std::atomic<ConnectionStatus> connection_status_ = ConnectionStatus::Closed;
  TraversalMode traversal_mode_ = TraversalMode::UnknownMode;
  int fps_ = 0;
  int frame_count_ = 0;
  std::chrono::steady_clock::time_point last_time_;
  XNetTrafficStats net_traffic_stats_{};

  using QueuedFile = FileTransferState::QueuedFile;
  using FileTransferStatus = FileTransferState::FileTransferStatus;
  using FileTransferInfo = FileTransferState::FileTransferInfo;
  FileTransferState file_transfer_;
};

using RemoteSessionPtr = std::shared_ptr<RemoteSession>;

}  // namespace crossdesk::gui_detail

#endif  // CROSSDESK_GUI_REMOTE_SESSION_H_
