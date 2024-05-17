#ifndef _SCREEN_CAPTURER_WGC_H_
#define _SCREEN_CAPTURER_WGC_H_

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "screen_capturer.h"
#include "wgc_session.h"
#include "wgc_session_impl.h"

class ScreenCapturerWgc : public ScreenCapturer,
                          public WgcSession::wgc_session_observer {
 public:
  ScreenCapturerWgc();
  ~ScreenCapturerWgc();

 public:
  bool IsWgcSupported();

  virtual int Init(const RECORD_DESKTOP_RECT &rect, const int fps,
                   cb_desktop_data cb);
  virtual int Destroy();

  virtual int Start();

  int Pause();
  int Resume();
  int Stop();

  void OnFrame(const WgcSession::wgc_session_frame &frame);

 protected:
  void CleanUp();

 private:
  WgcSession *session_ = nullptr;

  std::atomic_bool _running;
  std::atomic_bool _paused;
  std::atomic_bool _inited;

  std::thread _thread;

  std::string _device_name;

  RECORD_DESKTOP_RECT _rect;

  int _fps;

  cb_desktop_data _on_data = nullptr;

  unsigned char *nv12_frame_ = nullptr;
  unsigned char *nv12_frame_scaled_ = nullptr;
};

#endif