#ifndef _SCREEN_CAPTURER_WGC_H_
#define _SCREEN_CAPTURER_WGC_H_

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

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

  virtual int Init(const int fps, cb_desktop_data cb) override;
  virtual int Destroy() override;
  virtual int Start() override;
  virtual int Stop() override;

  int Pause();
  int Resume();

  std::vector<DisplayInfo> GetDisplayList() { return display_list_; }

  int SwitchTo(int monitor_index);

  void OnFrame(const WgcSession::wgc_session_frame &frame);

 protected:
  void CleanUp();

 private:
  HMONITOR monitor_;
  MONITORINFOEX monitor_info_;
  std::vector<DisplayInfo> display_list_;

 private:
  WgcSession *session_ = nullptr;

  std::atomic_bool _running;
  std::atomic_bool _paused;
  std::atomic_bool _inited;

  std::thread _thread;

  std::string _device_name;

  int _fps;

  cb_desktop_data _on_data = nullptr;

  unsigned char *nv12_frame_ = nullptr;
  unsigned char *nv12_frame_scaled_ = nullptr;
};

#endif