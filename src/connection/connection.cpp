#include "connection.h"

#include <fstream>
#include <iostream>
#include <thread>

#include "log.h"
#include "platform.h"

void ServerReceiveVideoBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {}

void ClientReceiveVideoBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {}

void ServerReceiveAudioBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {}

void ClientReceiveAudioBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {}

void ServerReceiveDataBuffer(const char *data, size_t size, const char *user_id,
                             size_t user_id_size) {}

void ClientReceiveDataBuffer(const char *data, size_t size, const char *user_id,
                             size_t user_id_size) {}

void ServerSignalStatus(SignalStatus status) {}

void ClientSignalStatus(SignalStatus status) {}

void ServerConnectionStatus(ConnectionStatus status) {}

void ClientConnectionStatus(ConnectionStatus status) {}

Connection::Connection() {}

Connection::~Connection() {}

int Connection::DeskConnectionInit() {
  std::string default_cfg_path = "../../../../config/config.ini";
  std::ifstream f(default_cfg_path.c_str());

  std::string mac_addr_str = GetMac();
  mac_addr_str_ = mac_addr_str;

  server_params_.cfg_path =
      f.good() ? "../../../../config/config.ini" : "config.ini";
  server_params_.on_receive_video_buffer = ServerReceiveVideoBuffer;
  server_params_.on_receive_audio_buffer = ServerReceiveAudioBuffer;
  server_params_.on_receive_data_buffer = ServerReceiveDataBuffer;
  server_params_.on_signal_status = ServerSignalStatus;
  server_params_.on_connection_status = ServerConnectionStatus;

  client_params_.cfg_path =
      f.good() ? "../../../../config/config.ini" : "config.ini";
  client_params_.on_receive_video_buffer = ClientReceiveVideoBuffer;
  client_params_.on_receive_audio_buffer = ClientReceiveAudioBuffer;
  client_params_.on_receive_data_buffer = ClientReceiveDataBuffer;
  client_params_.on_signal_status = ClientSignalStatus;
  client_params_.on_connection_status = ClientConnectionStatus;

  peer_server_ = CreatePeer(&server_params_);
  LOG_INFO("Create peer_server_");
  server_user_id_ = "S-" + mac_addr_str_;
  Init(peer_server_, server_user_id_.c_str());
  LOG_INFO("peer_server_ init finish");

  peer_client_ = CreatePeer(&client_params_);
  LOG_INFO("Create peer_client_");
  client_user_id_ = "C-" + mac_addr_str_;
  Init(peer_client_, client_user_id_.c_str());
  LOG_INFO("peer_client_ init finish");

  while (SignalStatus::SignalConnected != server_signal_status_) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

#if 0
  // Screen capture
  screen_capturer_factory_ = new ScreenCapturerFactory();
  screen_capturer_ = (ScreenCapturer *)screen_capturer__factory_->Create();

  last_frame_time_ = std::chrono::high_resolution_clock::now();
  ScreenCapturer::RECORD_DESKTOP_RECT rect;
  rect.left = 0;
  rect.top = 0;
  rect.right = screen_width;
  rect.bottom = screen_height;

  int screen_capturer_init_ret = screen_capturer_->Init(
      rect, 60,
      [](unsigned char *data, int size, int width, int height) -> void {
        auto now_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = now_time - last_frame_time_;
        auto tc = duration.count() * 1000;

        if (tc >= 0) {
          SendData(peer_server_, DATA_TYPE::VIDEO, (const char *)data,
                   NV12_BUFFER_SIZE);
          last_frame_time_ = now_time;
        }
      });

  if (0 == screen_capturer_init_ret) {
    screen_capturer_->Start();
  } else {
    screen_capturer_->Destroy();
    screen_capturer_ = nullptr;
  }

  // Mouse control
  device_controller_factory_ = new DeviceControllerFactory();
  mouse_controller_ = (MouseController *)device_controller_factory_->Create(
      DeviceControllerFactory::Device::Mouse);
  int mouse_controller_init_ret = mouse_controller_->Init(screen_w, screen_h);
  if (0 != mouse_controller_init_ret) {
    mouse_controller_->Destroy();
    mouse_controller_ = nullptr;
  }
#endif

  return 0;
}

int Connection::DeskConnectionCreate(const char *input_password) {
  input_password_ = input_password;
  is_created_connection_ = CreateConnection(peer_server_, mac_addr_str_.c_str(),
                                            input_password_.c_str())
                               ? false
                               : true;
  return 0;
}