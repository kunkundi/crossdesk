/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-30
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONTROLLER_SESSION_H_
#define _CONTROLLER_SESSION_H_

#include <chrono>
#include <deque>
#include <minirtc.h>
#include <mutex>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <remote_action.h>
#include <web_clipboard_protocol.h>
#include <set>
#include <string>
#include <vector>

namespace crossdesk {
struct EncodedVideoFrame {
  std::vector<uint8_t> data;
  uint64_t generation = 0, sequence = 0, timestamp_us = 0;
  bool key = false;
};
struct DecodedFrame {
  unsigned width = 0, height = 0;
  uint64_t generation = 0, frame_id = 0;
  std::chrono::steady_clock::time_point queued;
  std::vector<uint8_t> nv12;
};
// One replaceable pending picture; Take transfers ownership to the consumer.
// Callback memory is copied synchronously. Native descriptors never escape.
class LatestFrame {
public:
  bool Submit(const MiniRtcVideoFrame &, uint64_t generation);
  // Recycle the caller's old buffer only when a new frame is available.
  std::optional<DecodedFrame> Take(DecodedFrame *recycle = nullptr);
  void Reset(uint64_t generation);
  nlohmann::json Stats() const;

private:
  mutable std::mutex mutex_;
  std::optional<DecodedFrame> pending_;
  std::vector<uint8_t> spare_;
  uint64_t generation_ = 0, received_ = 0, dropped_ = 0, invalid_ = 0;
};
struct SessionOptions {
  std::string host = "api.crossdesk.cn", log_directory;
  int port = 9099, display = 0, fps = 8;
  int preview_width = 1920, preview_height = 1080;
  int quality = 2, max_bitrate_bps = 6000000;
  bool hardware_acceleration = false;
  std::chrono::seconds timeout{30};
};
// All public methods except the destructor run on one owner thread. Call Tick
// regularly. Library callbacks only enqueue bounded events or copy one frame.
class ControllerSession {
public:
  ControllerSession() = default;
  ~ControllerSession();
  void Connect(SessionOptions options, std::string device,
               std::string password);
  void Tick();
  void Disconnect();
  bool SelectDisplay(int index);
  void SetVideoOutput(bool enabled, bool request_keyframe = false);
  std::optional<EncodedVideoFrame> TakeEncodedVideo();
  void SetPreview(int fps, int width, int height, int quality = -1,
                  int max_bitrate_bps = -1);
  void SetControl(bool enabled);
  void ReleaseInput();
  bool Text(const std::string &utf8);
  bool Key(int key, bool down);
  bool Pointer(float x, float y, int flag, int wheel = 0);
  uint64_t RequestRemote(const std::string &method, nlohmann::json arguments = {});
  std::optional<nlohmann::json> TakeRemoteResult(uint64_t request);
  void ForgetRemoteRequest(uint64_t request) { remote_results_.erase(request); }
  uint64_t Clipboard(bool write, const std::string &text = {});
  void BeginInput();
  std::string InputResult();
  bool InputAcknowledged() const { return remote_capabilities_.value("input_ack", nlohmann::json{}) == true; }
  std::optional<DecodedFrame> TakeFrame(DecodedFrame *recycle = nullptr);
  nlohmann::json Status() const;
  bool Connected() const { return state_ == "connected"; }
  bool ControlEnabled() const { return control_; }
  bool PreviewAccepted() const { return preview_accepted_; }
  bool SupportsPreviewFps15() const { return fps15_; }
  bool Finished() const { return state_ == "failed" || state_ == "closed"; }
  uint64_t Generation() const { return generation_; }

private:
  struct Event {
    int kind, code;
    std::string data;
  };
  void Enqueue(Event event) noexcept;
  void Fail(const char *reason);
  void ClosePeers();
  Params Parameters();
  bool Send(const char *stream, const std::string &payload);
  bool SendInput(const char *stream, const RemoteAction &action);
  void Preview();
  static void Signal(SignalStatus, const char *, size_t, void *);
  static void Connection(ConnectionStatus, const char *, size_t, void *);
  static void Data(const char *, size_t, const char *, size_t, const char *,
                   size_t, void *);
  static void Video(const MiniRtcVideoFrame *, const char *, size_t,
                    const char *, size_t, void *);
  static int EncodedVideo(const uint8_t *, size_t, int64_t, const char *,
                          size_t, const char *, size_t, void *);
  static void Network(const char *, size_t, TraversalMode,
                      const MiniRtcNetTrafficStats *, const char *, size_t,
                      void *);
  SessionOptions options_;
  PeerPtr *peer_ = nullptr;
  std::string state_ = "idle", error_, device_, password_, client_id_;
  nlohmann::json remote_capabilities_ = nlohmann::json::object();
  uint64_t remote_sequence_ = 0, capability_request_ = 0;
  std::map<uint64_t, nlohmann::json> remote_results_;
  std::vector<uint64_t> input_requests_;
  std::string input_result_ = "applied";
  bool input_ack_active_ = false;
  bool clipboard_ready_ = false;
  web_clipboard::Assembler clipboard_assembler_;
  uint64_t generation_ = 0;
  uint32_t preview_request_ = 0, preview_ack_ = 0;
  bool control_ = false, preview_supported_ = false, resolution_ = false;
  bool fps15_ = false, bandwidth_ = false, text_supported_ = false;
  bool preview_accepted_ = false;
  HostPlatform host_platform_ = HostPlatform::Unknown;
  std::vector<HostDisplay> displays_;
  std::set<int> keys_, buttons_;
  float pointer_x_ = .5f, pointer_y_ = .5f;
  std::chrono::steady_clock::time_point deadline_;
  mutable std::mutex mutex_;
  std::deque<Event> events_;
  bool stopping_ = true, overflow_ = false;
  uint64_t callback_generation_ = 0;
  int callback_display_ = 0;
  MiniRtcNetTrafficStats network_{};
  TraversalMode traversal_ = UnknownMode;
  LatestFrame frames_;
  bool video_output_ = false, video_wait_key_ = true, video_request_key_ = false;
  size_t video_bytes_ = 0;
  uint64_t video_generation_ = 0, video_sequence_ = 0;
  std::deque<EncodedVideoFrame> video_frames_;
  std::vector<uint8_t> video_sps_, video_pps_;
  std::chrono::steady_clock::time_point video_key_requested_{};
};
nlohmann::json ParseBoundedJson(const std::string &text);
std::string SafeText(const std::string &text, size_t limit = 160);
} // namespace crossdesk

#endif
