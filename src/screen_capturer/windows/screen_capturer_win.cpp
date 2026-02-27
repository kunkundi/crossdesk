#include "screen_capturer_win.h"

#include <memory>

#include "rd_log.h"
#include "screen_capturer_dxgi.h"
#include "screen_capturer_gdi.h"
#include "screen_capturer_wgc.h"

namespace crossdesk {

ScreenCapturerWin::ScreenCapturerWin() {}
ScreenCapturerWin::~ScreenCapturerWin() { Destroy(); }

int ScreenCapturerWin::Init(const int fps, cb_desktop_data cb) {
  fps_ = fps;
  cb_ = cb;

  int ret = -1;

  impl_ = std::make_unique<ScreenCapturerWgc>();
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using WGC");
    return 0;
  }

  LOG_WARN("Windows capturer: WGC init failed (ret={}), try DXGI", ret);
  impl_.reset();

  impl_ = std::make_unique<ScreenCapturerDxgi>();
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using DXGI Desktop Duplication");
    return 0;
  }

  LOG_WARN("Windows capturer: DXGI init failed (ret={}), fallback to GDI", ret);
  impl_.reset();

  impl_ = std::make_unique<ScreenCapturerGdi>();
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using GDI BitBlt");
    return 0;
  }

  LOG_ERROR("Windows capturer: all implementations failed, ret={}", ret);
  impl_.reset();
  return -1;
}

int ScreenCapturerWin::Destroy() {
  if (impl_) {
    impl_->Destroy();
    impl_.reset();
  }
  return 0;
}

int ScreenCapturerWin::Start(bool show_cursor) {
  if (!impl_) return -1;
  return impl_->Start(show_cursor);
}

int ScreenCapturerWin::Stop() {
  if (!impl_) return 0;
  return impl_->Stop();
}

int ScreenCapturerWin::Pause(int monitor_index) {
  if (!impl_) return -1;
  return impl_->Pause(monitor_index);
}

int ScreenCapturerWin::Resume(int monitor_index) {
  if (!impl_) return -1;
  return impl_->Resume(monitor_index);
}

int ScreenCapturerWin::SwitchTo(int monitor_index) {
  if (!impl_) return -1;
  return impl_->SwitchTo(monitor_index);
}

std::vector<DisplayInfo> ScreenCapturerWin::GetDisplayInfoList() {
  if (!impl_) return {};
  return impl_->GetDisplayInfoList();
}

}  // namespace crossdesk