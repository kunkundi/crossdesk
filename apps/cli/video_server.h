/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-03
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _VIDEO_SERVER_H_
#define _VIDEO_SERVER_H_

#include <controller_session.h>
#include <memory>
#include <string>

namespace crossdesk::cli {

struct VideoGrant {
  std::string ticket, view, session, origin;
  uint64_t view_generation = 0;
  bool apps = false;
};

// Owns the loopback listener and its I/O thread. JSON-RPC carries only grants
// and diagnostics; encoded and decoded pictures stay in the native process.
class VideoServer {
public:
  VideoServer();
  ~VideoServer();
  VideoServer(const VideoServer &) = delete;
  VideoServer &operator=(const VideoServer &) = delete;

  std::string Url() const;
  void Bind(uint64_t generation, bool decoded, bool connected);
  void Grant(VideoGrant grant);
  void Revoke(const std::string &view = "");
  bool Enabled() const;
  bool TakeKeyframeRequest();
  void Submit(std::shared_ptr<const DecodedFrame> frame);
  void Submit(EncodedVideoFrame frame);
  nlohmann::json Status() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace crossdesk::cli

#endif
