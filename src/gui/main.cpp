#include <SDL.h>
#include <stdio.h>
#ifdef _WIN32
#ifdef REMOTE_DESK_DEBUG
#pragma comment(linker, "/subsystem:\"console\"")
#else
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif
#endif

#include <stdio.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "../../thirdparty/projectx/src/interface/x.h"
#include "device_controller_factory.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "log.h"
#include "platform.h"
#include "screen_capturer_factory.h"

#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

#ifdef REMOTE_DESK_DEBUG
#define MOUSE_CONTROL 0
#else
#define MOUSE_CONTROL 1
#endif

int screen_w = 1280, screen_h = 720;
int window_w = 1280, window_h = 720;
const int pixel_w = 1280, pixel_h = 720;

unsigned char dst_buffer[pixel_w * pixel_h * 3 / 2];
unsigned char audio_buffer[960];
SDL_Texture *sdlTexture = nullptr;
SDL_Renderer *sdlRenderer = nullptr;
SDL_Rect sdlRect;
SDL_Window *window;
static SDL_AudioDeviceID input_dev;
static SDL_AudioDeviceID output_dev;

uint32_t start_time, end_time, elapsed_time;
uint32_t frame_count = 0;
int fps = 0;

static std::atomic<bool> audio_buffer_fresh{false};
static uint32_t last_ts = 0;

// int64_t src_ch_layout = AV_CH_LAYOUT_MONO;
// int src_rate = 48000;
// enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_S16;
// int src_nb_channels = 0;
// uint8_t **src_data = NULL;
// int src_linesize;
// int src_nb_samples = 480;

// int64_t dst_ch_layout = AV_CH_LAYOUT_MONO;
// int dst_rate = 48000;
// enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;
// int dst_nb_channels = 0;
// uint8_t **dst_data = NULL;
// int dst_linesize;
// int dst_nb_samples;
// int max_dst_nb_samples;

int dst_bufsize;
struct SwrContext *swr_ctx;

int ret;

int audio_len = 0;

std::string window_title = "Remote Desk Client";
std::string server_connection_status_str = "-";
std::string client_connection_status_str = "-";
std::string server_signal_status_str = "-";
std::string client_signal_status_str = "-";

std::atomic<ConnectionStatus> server_connection_status{
    ConnectionStatus::Closed};
std::atomic<ConnectionStatus> client_connection_status{
    ConnectionStatus::Closed};
std::atomic<SignalStatus> server_signal_status{SignalStatus::SignalClosed};
std::atomic<SignalStatus> client_signal_status{SignalStatus::SignalClosed};

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define QUIT_EVENT (SDL_USEREVENT + 2)

typedef struct {
  char password[7];
} CDCache;

int thread_exit = 0;
PeerPtr *peer_server = nullptr;
PeerPtr *peer_client = nullptr;
bool joined = false;
bool received_frame = false;
bool menu_hovered = false;

static bool connect_button_pressed = false;
static const char *connect_label = "Connect";
static char input_password[7] = "";
static FILE *cd_cache_file = nullptr;
static CDCache cd_cache;

static bool is_create_connection = false;
static bool done = false;

ScreenCapturerFactory *screen_capturer_factory = nullptr;
ScreenCapturer *screen_capturer = nullptr;

DeviceControllerFactory *device_controller_factory = nullptr;
MouseController *mouse_controller = nullptr;

char *nv12_buffer = nullptr;

#ifdef __linux__
std::chrono::_V2::system_clock::time_point last_frame_time_;
#else
std::chrono::steady_clock::time_point last_frame_time_;
#endif

inline int ProcessMouseKeyEven(SDL_Event &ev) {
  float ratio = (float)(1280.0 / window_w);

  RemoteAction remote_action;
  remote_action.m.x = (size_t)(ev.button.x * ratio);
  remote_action.m.y = (size_t)(ev.button.y * ratio);

  if (SDL_KEYDOWN == ev.type)  // SDL_KEYUP
  {
    // printf("SDLK_DOWN: %d\n", SDL_KeyCode(ev.key.keysym.sym));
    if (SDLK_DOWN == ev.key.keysym.sym) {
      // printf("SDLK_DOWN  \n");

    } else if (SDLK_UP == ev.key.keysym.sym) {
      // printf("SDLK_UP  \n");

    } else if (SDLK_LEFT == ev.key.keysym.sym) {
      // printf("SDLK_LEFT  \n");

    } else if (SDLK_RIGHT == ev.key.keysym.sym) {
      // printf("SDLK_RIGHT  \n");
    }
  } else if (SDL_MOUSEBUTTONDOWN == ev.type) {
    remote_action.type = ControlType::mouse;
    if (SDL_BUTTON_LEFT == ev.button.button) {
      remote_action.m.flag = MouseFlag::left_down;
    } else if (SDL_BUTTON_RIGHT == ev.button.button) {
      remote_action.m.flag = MouseFlag::right_down;
    }
    SendData(peer_client, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_MOUSEBUTTONUP == ev.type) {
    remote_action.type = ControlType::mouse;
    if (SDL_BUTTON_LEFT == ev.button.button) {
      remote_action.m.flag = MouseFlag::left_up;
    } else if (SDL_BUTTON_RIGHT == ev.button.button) {
      remote_action.m.flag = MouseFlag::right_up;
    }
    SendData(peer_client, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_MOUSEMOTION == ev.type) {
    remote_action.type = ControlType::mouse;
    remote_action.m.flag = MouseFlag::move;
    SendData(peer_client, DATA_TYPE::DATA, (const char *)&remote_action,
             sizeof(remote_action));
  } else if (SDL_QUIT == ev.type) {
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
    printf("SDL_QUIT\n");
    return 0;
  }

  return 0;
}

void SdlCaptureAudioIn(void *userdata, Uint8 *stream, int len) {
  // int64_t delay = swr_get_delay(swr_ctx, src_rate);
  // dst_nb_samples = (int)av_rescale_rnd(delay + src_nb_samples, dst_rate,
  //                                      src_rate, AV_ROUND_UP);
  // if (dst_nb_samples > max_dst_nb_samples) {
  //   av_freep(&dst_data[0]);
  //   ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
  //                          dst_nb_samples, dst_sample_fmt, 1);
  //   if (ret < 0) return;
  //   max_dst_nb_samples = dst_nb_samples;
  // }

  // ret = swr_convert(swr_ctx, dst_data, dst_nb_samples,
  //                   (const uint8_t **)&stream, src_nb_samples);
  // if (ret < 0) {
  //   fprintf(stderr, "Error while converting\n");
  //   return;
  // }
  // dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
  // ret,
  //                                          dst_sample_fmt, 1);
  // if (dst_bufsize < 0) {
  //   fprintf(stderr, "Could not get sample buffer size\n");
  //   return;
  // }

  // if (1) {
  //   if ("ClientConnected" == client_connection_status_str) {
  //     SendData(peer_client, DATA_TYPE::AUDIO, (const char *)dst_data[0],
  //              dst_bufsize);
  //   }

  //   if ("ServerConnected" == server_connection_status_str) {
  //     SendData(peer_server, DATA_TYPE::AUDIO, (const char *)dst_data[0],
  //              dst_bufsize);
  //   }
  // } else {
  //   memcpy(audio_buffer, dst_data[0], dst_bufsize);
  //   audio_len = dst_bufsize;
  //   SDL_Delay(10);
  //   audio_buffer_fresh = true;
  // }
}

void SdlCaptureAudioOut(void *userdata, Uint8 *stream, int len) {
  // if ("ClientConnected" != client_connection_status_str) {
  //   return;
  // }

  if (!audio_buffer_fresh) {
    return;
  }

  SDL_memset(stream, 0, len);

  if (audio_len == 0) {
    return;
  } else {
  }

  len = (len > audio_len ? audio_len : len);
  SDL_MixAudioFormat(stream, audio_buffer, AUDIO_S16LSB, len,
                     SDL_MIX_MAXVOLUME);
  audio_buffer_fresh = false;
}

void ServerReceiveVideoBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {}

void ClientReceiveVideoBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {
  // std::cout << "Receive: [" << user_id << "] " << std::endl;
  if (joined) {
    memcpy(dst_buffer, data, size);

    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    received_frame = true;
  }
}

void ServerReceiveAudioBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {
  // memset(audio_buffer, 0, size);
  // memcpy(audio_buffer, data, size);
  // audio_len = size;
  audio_buffer_fresh = true;

  SDL_QueueAudio(output_dev, data, (Uint32)size);
  // printf("queue audio\n");
}

void ClientReceiveAudioBuffer(const char *data, size_t size,
                              const char *user_id, size_t user_id_size) {
  // std::cout << "Client receive audio, size " << size << ", user [" << user_id
  //           << "] " << std::endl;
  SDL_QueueAudio(output_dev, data, (Uint32)size);
}

void ServerReceiveDataBuffer(const char *data, size_t size, const char *user_id,
                             size_t user_id_size) {
  std::string user(user_id, user_id_size);

  RemoteAction remote_action;
  memcpy(&remote_action, data, sizeof(remote_action));

  // std::cout << "remote_action: " << remote_action.type << " "
  //           << remote_action.m.flag << " " << remote_action.m.x << " "
  //           << remote_action.m.y << std::endl;
#if MOUSE_CONTROL
  mouse_controller->SendCommand(remote_action);
#endif
}

void ClientReceiveDataBuffer(const char *data, size_t size, const char *user_id,
                             size_t user_id_size) {}

void ServerSignalStatus(SignalStatus status) {
  server_signal_status = status;
  if (SignalStatus::SignalConnecting == status) {
    server_signal_status_str = "ServerSignalConnecting";
  } else if (SignalStatus::SignalConnected == status) {
    server_signal_status_str = "ServerSignalConnected";
  } else if (SignalStatus::SignalFailed == status) {
    server_signal_status_str = "ServerSignalFailed";
  } else if (SignalStatus::SignalClosed == status) {
    server_signal_status_str = "ServerSignalClosed";
  } else if (SignalStatus::SignalReconnecting == status) {
    server_signal_status_str = "ServerSignalReconnecting";
  }
}

void ClientSignalStatus(SignalStatus status) {
  client_signal_status = status;
  if (SignalStatus::SignalConnecting == status) {
    client_signal_status_str = "ClientSignalConnecting";
  } else if (SignalStatus::SignalConnected == status) {
    client_signal_status_str = "ClientSignalConnected";
  } else if (SignalStatus::SignalFailed == status) {
    client_signal_status_str = "ClientSignalFailed";
  } else if (SignalStatus::SignalClosed == status) {
    client_signal_status_str = "ClientSignalClosed";
  } else if (SignalStatus::SignalReconnecting == status) {
    client_signal_status_str = "ClientSignalReconnecting";
  }
}

void ServerConnectionStatus(ConnectionStatus status) {
  server_connection_status = status;
  if (ConnectionStatus::Connecting == status) {
    server_connection_status_str = "ServerConnecting";
  } else if (ConnectionStatus::Connected == status) {
    server_connection_status_str = "ServerConnected";
  } else if (ConnectionStatus::Disconnected == status) {
    server_connection_status_str = "ServerDisconnected";
  } else if (ConnectionStatus::Failed == status) {
    server_connection_status_str = "ServerFailed";
  } else if (ConnectionStatus::Closed == status) {
    server_connection_status_str = "ServerClosed";
  } else if (ConnectionStatus::IncorrectPassword == status) {
    server_connection_status_str = "Incorrect password";
    if (connect_button_pressed) {
      connect_button_pressed = false;
      joined = false;
      connect_label = connect_button_pressed ? "Disconnect" : "Connect";
    }
  }
}

void ClientConnectionStatus(ConnectionStatus status) {
  client_connection_status = status;
  if (ConnectionStatus::Connecting == status) {
    client_connection_status_str = "ClientConnecting";
  } else if (ConnectionStatus::Connected == status) {
    client_connection_status_str = "ClientConnected";
    joined = true;
  } else if (ConnectionStatus::Disconnected == status) {
    client_connection_status_str = "ClientDisconnected";
  } else if (ConnectionStatus::Failed == status) {
    client_connection_status_str = "ClientFailed";
  } else if (ConnectionStatus::Closed == status) {
    client_connection_status_str = "ClientClosed";
  } else if (ConnectionStatus::IncorrectPassword == status) {
    client_connection_status_str = "Incorrect password";
    if (connect_button_pressed) {
      connect_button_pressed = false;
      joined = false;
      connect_label = connect_button_pressed ? "Disconnect" : "Connect";
    }
  } else if (ConnectionStatus::NoSuchTransmissionId == status) {
    client_connection_status_str = "No such transmission id";
    if (connect_button_pressed) {
      connect_button_pressed = false;
      joined = false;
      connect_label = connect_button_pressed ? "Disconnect" : "Connect";
    }
  }
}

int initResampler() {
  /* create resampler context */
  // swr_ctx = swr_alloc();
  // if (!swr_ctx) {
  //   fprintf(stderr, "Could not allocate resampler context\n");
  //   ret = AVERROR(ENOMEM);
  //   return -1;
  // }

  // /* set options */
  // av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
  // av_opt_set_int(swr_ctx, "in_sample_rate", src_rate, 0);
  // av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);

  // av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
  // av_opt_set_int(swr_ctx, "out_sample_rate", dst_rate, 0);
  // av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

  // /* initialize the resampling context */
  // if ((ret = swr_init(swr_ctx)) < 0) {
  //   fprintf(stderr, "Failed to initialize the resampling context\n");
  //   return -1;
  // }

  // /* allocate source and destination samples buffers */
  // src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);

  // ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize,
  //                                          src_nb_channels, src_nb_samples,
  //                                          src_sample_fmt, 0);
  // if (ret < 0) {
  //   fprintf(stderr, "Could not allocate source samples\n");
  //   return -1;
  // }

  // max_dst_nb_samples = dst_nb_samples =
  //     av_rescale_rnd(src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);

  // /* buffer is going to be directly written to a rawaudio file, no alignment
  // */ dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);

  // ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize,
  //                                          dst_nb_channels, dst_nb_samples,
  //                                          dst_sample_fmt, 0);
  // if (ret < 0) {
  //   fprintf(stderr, "Could not allocate destination samples\n");
  //   return -1;
  // }

  return 0;
}

int main(int argc, char *argv[]) {
  LOG_INFO("Remote desk");

  last_ts = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count());

  initResampler();

  cd_cache_file = fopen("cache.cd", "r+");
  if (cd_cache_file) {
    fseek(cd_cache_file, 0, SEEK_SET);
    fread(&cd_cache.password, sizeof(cd_cache.password), 1, cd_cache_file);
    fclose(cd_cache_file);
    strncpy(input_password, cd_cache.password, sizeof(cd_cache.password));
  }

  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
               SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

  // Create window with SDL_Renderer graphics context
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  window = SDL_CreateWindow("Remote Desk", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, window_w, window_h,
                            window_flags);

  SDL_DisplayMode DM;
  SDL_GetCurrentDisplayMode(0, &DM);
  screen_w = DM.w;
  screen_h = DM.h;

  sdlRenderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (sdlRenderer == nullptr) {
    SDL_Log("Error creating SDL_Renderer!");
    return 0;
  }

  Uint32 pixformat = 0;
  pixformat = SDL_PIXELFORMAT_NV12;

  sdlTexture = SDL_CreateTexture(sdlRenderer, pixformat,
                                 SDL_TEXTUREACCESS_STREAMING, pixel_w, pixel_h);

  // Audio
  SDL_AudioSpec want_in, have_in, want_out, have_out;
  SDL_zero(want_in);
  want_in.freq = 48000;
  want_in.format = AUDIO_S16LSB;
  want_in.channels = 1;
  want_in.samples = 480;
  want_in.callback = SdlCaptureAudioIn;

  input_dev = SDL_OpenAudioDevice(NULL, 1, &want_in, &have_in, 0);
  if (input_dev == 0) {
    SDL_Log("Failed to open input: %s", SDL_GetError());
    // return 1;
  }

  SDL_zero(want_out);
  want_out.freq = 48000;
  want_out.format = AUDIO_S16LSB;
  want_out.channels = 1;
  // want_out.silence = 0;
  want_out.samples = 480;
  want_out.callback = NULL;

  output_dev = SDL_OpenAudioDevice(NULL, 0, &want_out, &have_out, 0);
  if (output_dev == 0) {
    SDL_Log("Failed to open input: %s", SDL_GetError());
    // return 1;
  }

  SDL_PauseAudioDevice(input_dev, 0);
  SDL_PauseAudioDevice(output_dev, 0);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, sdlRenderer);
  ImGui_ImplSDLRenderer2_Init(sdlRenderer);

  // Our state
  bool show_demo_window = true;
  bool show_another_window = false;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  std::string mac_addr_str = GetMac();

  std::thread rtc_thread(
      [](int screen_width, int screen_height) {
        std::string default_cfg_path = "../../../../config/config.ini";
        std::ifstream f(default_cfg_path.c_str());

        std::string mac_addr_str = GetMac();

        Params server_params;
        server_params.cfg_path =
            f.good() ? "../../../../config/config.ini" : "config.ini";
        server_params.on_receive_video_buffer = ServerReceiveVideoBuffer;
        server_params.on_receive_audio_buffer = ServerReceiveAudioBuffer;
        server_params.on_receive_data_buffer = ServerReceiveDataBuffer;
        server_params.on_signal_status = ServerSignalStatus;
        server_params.on_connection_status = ServerConnectionStatus;

        Params client_params;
        client_params.cfg_path =
            f.good() ? "../../../../config/config.ini" : "config.ini";
        client_params.on_receive_video_buffer = ClientReceiveVideoBuffer;
        client_params.on_receive_audio_buffer = ClientReceiveAudioBuffer;
        client_params.on_receive_data_buffer = ClientReceiveDataBuffer;
        client_params.on_signal_status = ClientSignalStatus;
        client_params.on_connection_status = ClientConnectionStatus;

        std::string transmission_id = "000001";

        peer_server = CreatePeer(&server_params);
        LOG_INFO("Create peer_server");
        std::string server_user_id = "S-" + mac_addr_str;
        Init(peer_server, server_user_id.c_str());
        LOG_INFO("peer_server init finish");

        peer_client = CreatePeer(&client_params);
        LOG_INFO("Create peer_client");
        std::string client_user_id = "C-" + mac_addr_str;
        Init(peer_client, client_user_id.c_str());
        LOG_INFO("peer_client init finish");

        {
          while (SignalStatus::SignalConnected != server_signal_status &&
                 !done) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }

          if (done) {
            return;
          }

          std::string user_id = "S-" + mac_addr_str;
          is_create_connection =
              CreateConnection(peer_server, mac_addr_str.c_str(),
                               input_password)
                  ? false
                  : true;

          nv12_buffer = new char[NV12_BUFFER_SIZE];

          // Screen capture
          screen_capturer_factory = new ScreenCapturerFactory();
          screen_capturer = (ScreenCapturer *)screen_capturer_factory->Create();

          last_frame_time_ = std::chrono::high_resolution_clock::now();
          ScreenCapturer::RECORD_DESKTOP_RECT rect;
          rect.left = 0;
          rect.top = 0;
          rect.right = screen_w;
          rect.bottom = screen_h;

          screen_capturer->Init(
              rect, 60,
              [](unsigned char *data, int size, int width, int height) -> void {
                auto now_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> duration =
                    now_time - last_frame_time_;
                auto tc = duration.count() * 1000;

                if (tc >= 0) {
                  SendData(peer_server, DATA_TYPE::VIDEO, (const char *)data,
                           NV12_BUFFER_SIZE);
                  last_frame_time_ = now_time;
                }
              });

          screen_capturer->Start();

          // Mouse control
          device_controller_factory = new DeviceControllerFactory();
          mouse_controller =
              (MouseController *)device_controller_factory->Create(
                  DeviceControllerFactory::Device::Mouse);
          mouse_controller->Init(screen_w, screen_h);
        }
      },
      screen_w, screen_h);

  // Main loop
  while (!done) {
    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (joined && !menu_hovered) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    {
      static float f = 0.0f;
      static int counter = 0;

      const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);
      ImGui::SetNextWindowSize(ImVec2(190, 200));

      ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoResize);

      {
        menu_hovered = ImGui::IsWindowHovered();
        ImGui::Text(" LOCAL ID:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95);
        ImGui::InputText(
            "##local_id", (char *)mac_addr_str.c_str(),
            mac_addr_str.length() + 1,
            ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_ReadOnly);

        ImGui::Text(" PASSWORD:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95);

        char input_password_tmp[7] = "";
        strncpy(input_password_tmp, input_password, sizeof(input_password));

        ImGui::InputTextWithHint("##server_pwd", "max 6 chars", input_password,
                                 IM_ARRAYSIZE(input_password),
                                 ImGuiInputTextFlags_CharsNoBlank);
        if (strcmp(input_password_tmp, input_password)) {
          cd_cache_file = fopen("cache.cd", "w+");
          if (cd_cache_file) {
            fseek(cd_cache_file, 0, SEEK_SET);
            strncpy(cd_cache.password, input_password, sizeof(input_password));
            fwrite(&cd_cache.password, sizeof(cd_cache.password), 1,
                   cd_cache_file);
            fclose(cd_cache_file);
          }
          LeaveConnection(peer_server);
          CreateConnection(peer_server, mac_addr_str.c_str(), input_password);
        }

        ImGui::Spacing();

        ImGui::Separator();

        ImGui::Spacing();
        {
          {
            static char remote_id[20] = "";
            ImGui::Text("REMOTE ID:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(95);
            ImGui::InputTextWithHint("##remote_id", mac_addr_str.c_str(),
                                     remote_id, IM_ARRAYSIZE(remote_id),
                                     ImGuiInputTextFlags_CharsUppercase |
                                         ImGuiInputTextFlags_CharsNoBlank);

            ImGui::Spacing();

            ImGui::Text(" PASSWORD:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(95);
            static char client_password[20] = "";
            ImGui::InputTextWithHint("##client_pwd", "max 6 chars",
                                     client_password,
                                     IM_ARRAYSIZE(client_password),
                                     ImGuiInputTextFlags_CharsNoBlank);

            if (ImGui::Button(connect_label)) {
              int ret = -1;
              if ("ClientSignalConnected" == client_signal_status_str) {
                if (strcmp(connect_label, "Connect") == 0 && !joined) {
                  std::string user_id = "C-" + mac_addr_str;
                  ret = JoinConnection(peer_client, remote_id, client_password);
                  if (0 == ret) {
                    // joined = true;
                  }
                } else if (strcmp(connect_label, "Disconnect") == 0 && joined) {
                  ret = LeaveConnection(peer_client);
                  memset(audio_buffer, 0, 960);
                  if (0 == ret) {
                    joined = false;
                    received_frame = false;
                  }
                }

                if (0 == ret) {
                  connect_button_pressed = !connect_button_pressed;
                  connect_label =
                      connect_button_pressed ? "Disconnect" : "Connect";
                }
              }
            }
          }
        }
      }

      ImGui::Spacing();

      ImGui::Separator();

      ImGui::Spacing();

      {
        if (ImGui::Button("Resize Window")) {
          SDL_GetWindowSize(window, &window_w, &window_h);

          if (window_h != window_w * 9 / 16) {
            window_w = window_h * 16 / 9;
          }

          SDL_SetWindowSize(window, window_w, window_h);
        }
      }

      ImGui::End();
    }

    // Rendering
    ImGui::Render();
    SDL_RenderSetScale(sdlRenderer, io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        done = true;
      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_RESIZED) {
        SDL_GetWindowSize(window, &window_w, &window_h);
      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      } else if (event.type == REFRESH_EVENT) {
        sdlRect.x = 0;
        sdlRect.y = 0;
        sdlRect.w = window_w;
        sdlRect.h = window_h;

        SDL_UpdateTexture(sdlTexture, NULL, dst_buffer, pixel_w);
      } else {
        if (joined) {
          ProcessMouseKeyEven(event);
        }
      }
    }

    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);

    if (!joined || !received_frame) {
      SDL_RenderClear(sdlRenderer);
      SDL_SetRenderDrawColor(
          sdlRenderer, (Uint8)(clear_color.x * 0), (Uint8)(clear_color.y * 0),
          (Uint8)(clear_color.z * 0), (Uint8)(clear_color.w * 0));
    }

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(sdlRenderer);

    frame_count++;
    end_time = SDL_GetTicks();
    elapsed_time = end_time - start_time;
    if (elapsed_time >= 1000) {
      fps = frame_count / (elapsed_time / 1000);
      frame_count = 0;
      window_title = "Remote Desk Client FPS [" + std::to_string(fps) +
                     "] status [" + server_signal_status_str + "|" +
                     client_signal_status_str + "|" +
                     server_connection_status_str + "|" +
                     client_connection_status_str + "]";
      // For MacOS, UI frameworks can only be called from the main thread
      SDL_SetWindowTitle(window, window_title.c_str());
      start_time = end_time;
    }
  }

  // Cleanup

  if (is_create_connection) {
    LeaveConnection(peer_server);
  }

  if (joined) {
    LeaveConnection(peer_client);
  }

  rtc_thread.join();
  SDL_CloseAudioDevice(output_dev);
  SDL_CloseAudioDevice(input_dev);

  mouse_controller->Destroy();

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroyWindow(window);

  SDL_CloseAudio();
  SDL_Quit();

  return 0;
}