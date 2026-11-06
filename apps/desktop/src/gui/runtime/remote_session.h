/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _REMOTE_SESSION_H_
#define _REMOTE_SESSION_H_

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

#include <remote_action.h>
#include <preview_subscription.h>
#include <stream_names.h>
#include "display_info.h"
#include "features/gallery/preview_resolution.h"
#include "cursor_presentation.h"
#include "minirtc.h"
#include "rendering/video_latency.h"
#include "runtime/net_traffic_stats_cache.h"
#include "runtime/received_video_stats.h"
#include "runtime/connection_auth_state.h"

namespace crossdesk {
class PeerEventHandler;
namespace lan {
struct Session;
}
}  // namespace crossdesk

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
  std::atomic<bool> file_transfer_window_visible_ = false;
  std::atomic<uint32_t> current_file_id_{0};

  struct QueuedFile {
    std::filesystem::path file_path;
    std::string file_label;
    std::string remote_id;
    uint32_t file_id = 0;
  };
  std::queue<QueuedFile> file_send_queue_;
  std::mutex file_queue_mutex_;

  enum class FileTransferStatus { Queued, Sending, Completed, Failed, Receiving, Cancelling, Cancelled };

  struct FileTransferInfo {
    std::string file_name;
    std::string remote_id;
    std::filesystem::path file_path;
    uint64_t file_size = 0;
    FileTransferStatus status = FileTransferStatus::Queued;
    uint64_t sent_bytes = 0;
    uint32_t file_id = 0;
    uint32_t rate_bps = 0;
    bool receiving = false;
    uint32_t wire_file_id = 0;
  };
  std::vector<FileTransferInfo> file_transfer_list_;
  std::mutex file_transfer_list_mutex_;
};

// Runtime state for one connected or connecting remote endpoint. It groups the
// connection, media, input, window and transfer data that share one lifetime.
struct RemoteSession {
  // Wall membership, stream-window visibility and input ownership are separate.
  std::atomic<bool> wall_managed_{false};
  std::atomic<bool> preview_subscription_supported_{false};
  std::atomic<bool> preview_resolution_supported_{false};
  std::atomic<bool> preview_fps15_supported_{false};
  std::atomic<bool> preview_bandwidth_supported_{false};
  std::atomic<bool> preview_needs_sync_{true};
  std::atomic<uint32_t> preview_ack_{0};
  std::atomic<bool> preview_accepted_{false};
  uint32_t preview_request_ = 0;
  PreviewSubscription preview_requested_;
  gallery::PreviewResolutionDebouncer preview_resolution_;
  std::chrono::steady_clock::time_point preview_request_at_{};
  std::atomic<bool> clipboard_allowed_{false};
  std::atomic<uint64_t> input_generation_{0};
  std::atomic<bool> audio_allowed_{false};
  std::atomic<int> gallery_preview_width_{0};
  std::mutex gallery_frame_mutex_;
  std::shared_ptr<std::vector<unsigned char>> gallery_rgba_;
  int gallery_width_ = 0, gallery_height_ = 0;
  uint64_t gallery_sequence_ = 0;
  std::chrono::steady_clock::time_point gallery_frame_at_{};
  VideoLatencyFrame gallery_frame_timing_;
  // Keep preview submission estimates separate from stream presentation times.
  std::shared_ptr<VideoLatencyStats> gallery_video_latency_ =
      std::make_shared<VideoLatencyStats>();
  // Decoder-thread scratch memory, never shared with the UI.
  std::mutex gallery_conversion_mutex_;
  std::vector<unsigned char> gallery_nv12_, gallery_scaled_, gallery_scratch_;
  std::shared_ptr<lan::Session> lan_session_;
  MiniRtcParams params_;
  MiniRtcPeerPtr* peer_ = nullptr;
  std::shared_ptr<PeerEventHandler> peer_events_;
  std::atomic<bool> closing_{false};
  // Transport callbacks request tab closure; only the UI thread destroys it.
  std::atomic<bool> remote_close_pending_{false};
  std::string audio_label_ = kAudioStream;
  std::string data_label_ = kDataStream;
  std::string mouse_label_ = kMouseStream;
  std::string keyboard_label_ = kKeyboardStream;
  std::string file_label_ = kFileStream;
  std::string control_data_label_ = kControlStream;
  std::string file_feedback_label_ = kFileFeedbackStream;
  std::string clipboard_label_ = kClipboardStream;
  std::string local_id_;
  std::string remote_id_;
  bool exit_ = false;
  bool signal_connected_ = false;
  MiniRtcSignalStatus signal_status_ = MiniRtcSignalStatus::MiniRtcSignalClosed;
  bool connection_established_ = false;
  std::atomic<bool> was_connected_{false};
  // Transport callbacks publish changes; the UI thread persists history.
  std::atomic<bool> recent_connection_dirty_{false};
  bool rejoin_ = false;
  bool net_traffic_stats_button_pressed_ = false;
  bool enable_mouse_control_ = true;
  bool mouse_controller_is_started_ = false;
  bool audio_capture_button_pressed_ = true;
  bool control_mouse_ = true;
  bool streaming_ = false;
  bool p2p_mode_ = true;
  bool remember_password_ = false;
  char remote_password_[7] = "";
  ConnectionAuthState connection_auth_;
  // Reused across frames by the decode callback thread when a CPU NV12
  // native frame has to be materialized for a renderer that rejects it.
  std::vector<unsigned char> native_cpu_fallback_;

  // Written by the decode callback thread and consumed by the UI thread.
  std::mutex video_frame_mutex_;
  std::shared_ptr<std::vector<unsigned char>> front_frame_;
  std::shared_ptr<std::vector<unsigned char>> back_frame_;
  // Retains the most recent background CPU frame across disconnect cleanup
  // so closing an unselected tab can still update its recent-item thumbnail.
  std::shared_ptr<std::vector<unsigned char>> thumbnail_frame_;
  int thumbnail_width_ = 0;
  int thumbnail_height_ = 0;
  std::chrono::steady_clock::time_point background_snapshot_time_;
  bool render_rect_dirty_ = false;
  bool stream_cleanup_pending_ = false;
  int video_width_ = 0;
  int video_height_ = 0;
  int selected_display_ = 0;
  size_t video_size_ = 0;
  uint64_t video_frame_sequence_ = 0;
  VideoLatencyFrame video_frame_timing_;
  std::shared_ptr<VideoLatencyStats> video_latency_ =
      std::make_shared<VideoLatencyStats>();
  bool tab_selected_ = false;
  std::atomic<bool> tab_opened_{true};
  std::string remote_host_name_;
  // Host metadata and dismissal state belong to this connection and are
  // protected by remote_version_mutex_.
  std::mutex remote_version_mutex_;
  bool remote_host_info_received_ = false;
  std::string remote_app_version_;
  HostPlatform remote_platform_ = HostPlatform::Unknown;
  // Retain history metadata after disconnect resets the live capabilities.
  HostPlatform recent_platform_ = HostPlatform::Unknown;
  bool remote_update_dismissed_ = false;
  bool remote_service_status_received_ = false;
  std::mutex video_settings_mutex_;
  // Requested and confirmed settings for this remote session only.
  VideoSettings video_settings_{2, 60, 1};
  bool video_settings_supported_ = false;
  VideoSettings applied_video_settings_{2, 60, 1, 0, false};
  bool video_settings_pending_ = false;
  bool video_settings_failed_ = false;
  uint64_t video_settings_request_tick_ = 0;
  std::mutex privacy_status_mutex_;
  PrivacyStatus privacy_status_{};
  bool privacy_status_received_ = false;
  bool privacy_unlock_notice_ = false;
  bool privacy_desktop_available_ = true;
  bool privacy_command_pending_ = false;
  uint64_t privacy_command_tick_ = 0;
  uint32_t privacy_request_revision_ = 0;
  uint64_t privacy_status_tick_ = 0;
  bool remote_service_available_ = false;
  std::string remote_interactive_stage_;
  std::vector<DisplayInfo> display_info_list_;
  // Cursor snapshots arrive on the transport callback thread and are applied
  // by the Slint UI thread. Keep the snapshot atomic as a unit so visibility
  // and shape cannot briefly come from different protocol messages.
  std::mutex remote_cursor_state_mutex_;
  CursorState remote_cursor_state_{};
  bool remote_cursor_state_received_ = false;
  CursorPresentation cursor_presentation_;
  // Shared by minirtc callbacks, Slint rendering and SDL audio callbacks.
  std::atomic<MiniRtcConnectionStatus> connection_status_ = MiniRtcConnectionStatus::MiniRtcConnectionClosed;
  std::atomic<MiniRtcTraversalMode> traversal_mode_{MiniRtcTraversalMode::MiniRtcTraversalUnknown};
  int fps_ = 0;
  int frame_count_ = 0;
  std::chrono::steady_clock::time_point last_time_;
  // UI-thread snapshot refreshed with FPS once per second.
  std::optional<VideoLatencyStats::Snapshot> video_latency_snapshot_;
  NetTrafficStatsCache net_traffic_stats_;
  ReceivedVideoStats received_video_stats_;

  using QueuedFile = FileTransferState::QueuedFile;
  using FileTransferStatus = FileTransferState::FileTransferStatus;
  using FileTransferInfo = FileTransferState::FileTransferInfo;
  FileTransferState file_transfer_;
};

using RemoteSessionPtr = std::shared_ptr<RemoteSession>;

}  // namespace crossdesk::gui_detail

#endif
