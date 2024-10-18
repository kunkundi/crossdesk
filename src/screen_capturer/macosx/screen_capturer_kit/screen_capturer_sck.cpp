#include "screen_capturer_sck.h"

#include "rd_log.h"

ScreenCapturerSck::ScreenCapturerSck() {}
ScreenCapturerSck::~ScreenCapturerSck() {
  // if (inited_ && capture_thread_.joinable()) {
  //   capture_thread_.join();
  //   inited_ = false;
  // }
}

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
  // if (running_) {
  //   return 0;
  // }

  // running_ = true;
  // capture_thread_ = std::thread([this]() {
  //   while (running_) {
  //   }
  // });

  return 0;
}

int ScreenCapturerSck::Stop() { return 0; }

int ScreenCapturerSck::Pause() { return 0; }

int ScreenCapturerSck::Resume() { return 0; }

void ScreenCapturerSck::OnFrame() {}

void ScreenCapturerSck::CleanUp() {}