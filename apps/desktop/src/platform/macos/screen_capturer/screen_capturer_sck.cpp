#include "screen_capturer_sck.h"

#include "rd_log.h"

namespace crossdesk {

ScreenCapturerSck::ScreenCapturerSck() {}
ScreenCapturerSck::~ScreenCapturerSck() {}

void ScreenCapturerSck::SetPrivacyController(PrivacyController* privacy) {
  privacy_ = privacy;
  if (screen_capturer_sck_impl_)
    screen_capturer_sck_impl_->SetPrivacyController(privacy);
}

int ScreenCapturerSck::Init(const int fps, cb_desktop_data cb) {
  if (cb) {
    on_data_ = cb;
  } else {
    LOG_ERROR("cb is null");
    return -1;
  }

  screen_capturer_sck_impl_ = CreateScreenCapturerSck();
  screen_capturer_sck_impl_->SetPrivacyController(privacy_);
  const int ret = screen_capturer_sck_impl_->Init(fps, on_data_);
  if (ret != 0) {
    screen_capturer_sck_impl_.reset();
    return ret;
  }

  return 0;
}

void ScreenCapturerSck::SetCaptureFrameRate(int fps) {
  if (screen_capturer_sck_impl_) screen_capturer_sck_impl_->SetCaptureFrameRate(fps);
}

int ScreenCapturerSck::Destroy() {
  if (screen_capturer_sck_impl_) {
    screen_capturer_sck_impl_->Destroy();
  }
  return 0;
}

int ScreenCapturerSck::Start(bool show_cursor) {
  if (!screen_capturer_sck_impl_) {
    return -1;
  }

  return screen_capturer_sck_impl_->Start(show_cursor);
}

int ScreenCapturerSck::Stop() {
  if (screen_capturer_sck_impl_) {
    screen_capturer_sck_impl_->Stop();
  }
  return 0;
}

int ScreenCapturerSck::Pause(int monitor_index) {
  if (screen_capturer_sck_impl_) {
    return screen_capturer_sck_impl_->Pause(monitor_index);
  }
  return 0;
}

int ScreenCapturerSck::Resume(int monitor_index) {
  if (screen_capturer_sck_impl_) {
    return screen_capturer_sck_impl_->Resume(monitor_index);
  }
  return 0;
}

int ScreenCapturerSck::SwitchTo(int monitor_index) {
  if (screen_capturer_sck_impl_) {
    return screen_capturer_sck_impl_->SwitchTo(monitor_index);
  }

  return -1;
}

int ScreenCapturerSck::ResetToInitialMonitor() {
  if (screen_capturer_sck_impl_) {
    return screen_capturer_sck_impl_->ResetToInitialMonitor();
  }
  return -1;
}

std::vector<DisplayInfo> ScreenCapturerSck::GetDisplayInfoList() {
  if (screen_capturer_sck_impl_) {
    return screen_capturer_sck_impl_->GetDisplayInfoList();
  }

  return std::vector<DisplayInfo>();
}

bool ScreenCapturerSck::RefreshIdleDisplays(const std::function<bool(size_t)>& accept) {
  return screen_capturer_sck_impl_ && screen_capturer_sck_impl_->RefreshIdleDisplays(accept);
}

void ScreenCapturerSck::OnFrame() {}

void ScreenCapturerSck::CleanUp() {}
}  // namespace crossdesk
