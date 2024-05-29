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

#include "config_center.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "log.h"
#include "platform.h"

#define NV12_BUFFER_SIZE 1280 * 720 * 3 / 2

#ifdef REMOTE_DESK_DEBUG
#define MOUSE_CONTROL 0
#else
#define MOUSE_CONTROL 1
#endif

#define CHINESE_FONT 1

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

int dst_bufsize;
struct SwrContext *swr_ctx;

int ret;

int audio_len = 0;

std::string window_title = "Remote Desk Client";
std::string server_connection_status_str = "-";
std::string client_connection_status_str = "-";
std::string server_signal_status_str = "-";
std::string client_signal_status_str = "-";

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define QUIT_EVENT (SDL_USEREVENT + 2)

typedef struct {
  char password[7];
} CDCache;

bool joined = false;
bool received_frame = false;
bool menu_hovered = false;

static bool connect_button_pressed = false;
static bool fullscreen_button_pressed = false;

#if CHINESE_FONT
static const char *connect_label = u8"а╛╫с";
static const char *fullscreen_label = u8"х╚фа";
#else
static const char *connect_label = "Connect";
static const char *fullscreen_label = "FULLSCREEN";
#endif
static char input_password[7] = "";
static FILE *cd_cache_file = nullptr;
static CDCache cd_cache;

static bool is_create_connection = false;
static bool done = false;

ConfigCenter config_center;