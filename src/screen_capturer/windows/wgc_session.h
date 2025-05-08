#ifndef _WGC_SESSION_H_
#define _WGC_SESSION_H_

#include <Windows.h>

class WgcSession {
 public:
  struct wgc_session_frame {
    unsigned int width;
    unsigned int height;
    unsigned int row_pitch;

    const unsigned char *data;
  };

  class wgc_session_observer {
   public:
    virtual ~wgc_session_observer() {}
    virtual void OnFrame(const wgc_session_frame &frame) = 0;
  };

 public:
  virtual void Release() = 0;

  virtual int Initialize(HWND hwnd) = 0;
  virtual int Initialize(HMONITOR hmonitor) = 0;

  virtual void RegisterObserver(wgc_session_observer *observer) = 0;

  virtual int Start() = 0;
  virtual int Stop() = 0;

  virtual int Pause() = 0;
  virtual int Resume() = 0;

  virtual ~WgcSession(){};
};

#endif