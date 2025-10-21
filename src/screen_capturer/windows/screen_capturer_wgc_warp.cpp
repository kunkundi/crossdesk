#include <Windows.h>
#include <d3d11_4.h>

#include <iostream>
#include <thread>

#include "libyuv.h"
#include "rd_log.h"
#include "screen_capturer_wgc.h"

// Dummy window proc for hidden window
static LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ======================= 构造函数 / 析构函数 =======================
ScreenCapturerWgc::ScreenCapturerWgc()
    : monitor_(nullptr),
      hwnd_(nullptr),
      monitor_index_(0),
      running_(false),
      inited_(false),
      fps_(60),
      on_data_(nullptr),
      nv12_frame_(nullptr),
      nv12_frame_scaled_(nullptr) {}

ScreenCapturerWgc::~ScreenCapturerWgc() {
  Stop();
  CleanUp();

  if (nv12_frame_) {
    delete[] nv12_frame_;
    nv12_frame_ = nullptr;
  }

  if (nv12_frame_scaled_) {
    delete[] nv12_frame_scaled_;
    nv12_frame_scaled_ = nullptr;
  }
}

// ======================= 隐藏窗口创建 =======================
bool ScreenCapturerWgc::CreateHiddenWindow() {
  const wchar_t kClassName[] = L"ScreenCapturerHiddenWindow";

  WNDCLASSW wc = {};
  wc.lpfnWndProc = DummyWndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = kClassName;

  if (!RegisterClassW(&wc)) {
    std::cerr << "Failed to register dummy window class\n";
    return false;
  }

  hwnd_ = CreateWindowW(kClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
                        nullptr, nullptr, wc.hInstance, nullptr);
  if (!hwnd_) {
    std::cerr << "Failed to create dummy window\n";
    return false;
  }

  ShowWindow(hwnd_, SW_HIDE);
  return true;
}

// ======================= 初始化 =======================
int ScreenCapturerWgc::Init(const int fps, cb_desktop_data cb) {
  if (inited_) return 0;

  fps_ = fps;
  on_data_ = cb;

  // 创建隐藏窗口
  if (!CreateHiddenWindow()) {
    return -1;
  }

  // 初始化 WGC Session
  sessions_.push_back(
      {std::make_unique<WgcSessionImpl>(0), false, false, false});
  sessions_.back().session_->RegisterObserver(this);
  int error = sessions_.back().session_->Initialize(hwnd_);
  if (error != 0) {
    std::cerr << "WGC session init failed\n";
    return error;
  }
  sessions_[0].inited_ = true;

  inited_ = true;
  return 0;
}

int ScreenCapturerWgc::Destroy() {
  Stop();
  CleanUp();
  return 0;
}

int ScreenCapturerWgc::Pause(int monitor_index) {
  // 目前只支持隐藏窗口，所以忽略 monitor_index
  if (!running_) return -1;
  sessions_[0].session_->Pause();
  sessions_[0].paused_ = true;
  return 0;
}

int ScreenCapturerWgc::Resume(int monitor_index) {
  if (!running_) return -1;
  sessions_[0].session_->Resume();
  sessions_[0].paused_ = false;
  return 0;
}

int ScreenCapturerWgc::SwitchTo(int monitor_index) {
  // 单隐藏窗口模式，不支持切换
  return 0;
}

// ======================= 开始 =======================
int ScreenCapturerWgc::Start() {
  if (!inited_) return -1;
  if (running_) return 0;
  if (sessions_.empty()) return -1;

  sessions_[0].session_->Start();
  sessions_[0].running_ = true;
  running_ = true;

  return 0;
}

// ======================= 停止 =======================
int ScreenCapturerWgc::Stop() {
  if (!running_) return 0;

  if (!sessions_.empty()) {
    sessions_[0].session_->Stop();
    sessions_[0].running_ = false;
  }

  running_ = false;

  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }

  inited_ = false;
  sessions_.clear();

  return 0;
}

// ======================= 帧回调 =======================
void ScreenCapturerWgc::OnFrame(const WgcSession::wgc_session_frame& frame,
                                int id) {
  if (!on_data_) {
    return;
  }

  if (!nv12_frame_) {
    nv12_frame_ = new unsigned char[frame.width * frame.height * 3 / 2];
  }

  libyuv::ARGBToNV12((const uint8_t*)frame.data, frame.width * 4,
                     (uint8_t*)nv12_frame_, frame.width,
                     (uint8_t*)(nv12_frame_ + frame.width * frame.height),
                     frame.width, frame.width, frame.height);

  on_data_(nv12_frame_, frame.width * frame.height * 3 / 2, frame.width,
           frame.height, "hidden_window");
}

// ======================= 清理 =======================
void ScreenCapturerWgc::CleanUp() {
  if (inited_) {
    for (auto& session : sessions_) {
      if (session.session_) {
        session.session_->Stop();
      }
    }
    sessions_.clear();
  }
}
