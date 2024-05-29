/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <chrono>
#include <string>

#include "../../thirdparty/projectx/src/interface/x.h"
#include "device_controller_factory.h"
#include "screen_capturer_factory.h"

class Connection {
 public:
  Connection();
  ~Connection();

 public:
  int DeskConnectionInit(const char *input_password, int screen_width,
                         int screen_height);
  int DeskConnectionCreate();

 private:
  PeerPtr *peer_server_ = nullptr;
  PeerPtr *peer_client_ = nullptr;

  std::string mac_addr_str_ = "";

  Params server_params_;
  Params client_params_;

  std::string server_user_id_ = "";
  std::string client_user_id_ = "";

  std::string input_password_ = "";

  ScreenCapturerFactory *screen_capturer_factory_ = nullptr;
  ScreenCapturer *screen_capturer_ = nullptr;

  DeviceControllerFactory *device_controller_factory_ = nullptr;
  MouseController *mouse_controller_ = nullptr;

  bool is_created_connection_ = false;

#ifdef __linux__
  std::chrono::_V2::system_clock::time_point last_frame_time_;
#else
  std::chrono::steady_clock::time_point last_frame_time_;
#endif

  std::atomic<ConnectionStatus> server_connection_status_{
      ConnectionStatus::Closed};
  std::atomic<ConnectionStatus> client_connection_status_{
      ConnectionStatus::Closed};
  std::atomic<SignalStatus> server_signal_status_{SignalStatus::SignalClosed};
  std::atomic<SignalStatus> client_signal_status_{SignalStatus::SignalClosed};
};

#endif