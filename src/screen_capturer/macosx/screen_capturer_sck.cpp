#include "screen_capturer_sck.h"

#include "rd_log.h"

ScreenCapturerSck::ScreenCapturerSck() {}
ScreenCapturerSck::~ScreenCapturerSck() {}

int ScreenCapturerSck::Init(const int fps, cb_desktop_data cb) {
  if (cb) {
    on_data_ = cb;
  } else {
    LOG_ERROR("cb is null");
    return -1;
  }

  screen_capturer_sck_impl_ = CreateScreenCapturerSck();
  screen_capturer_sck_impl_->Init(fps, on_data_);

  return 0;
}

int ScreenCapturerSck::Destroy() { return 0; }

int ScreenCapturerSck::Start() {
  screen_capturer_sck_impl_->Start();

  return 0;
}

int ScreenCapturerSck::Stop() { return 0; }

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

std::vector<DisplayInfo> ScreenCapturerSck::GetDisplayInfoList() {
  if (screen_capturer_sck_impl_) {
    return screen_capturer_sck_impl_->GetDisplayInfoList();
  }

  return std::vector<DisplayInfo>();
}

void ScreenCapturerSck::OnFrame() {}

void ScreenCapturerSck::CleanUp() {}