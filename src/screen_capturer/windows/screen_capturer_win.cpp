#include "screen_capturer_win.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "rd_log.h"
#include "screen_capturer_dxgi.h"
#include "screen_capturer_gdi.h"
#include "screen_capturer_wgc.h"

namespace crossdesk {

ScreenCapturerWin::ScreenCapturerWin() {}
ScreenCapturerWin::~ScreenCapturerWin() { Destroy(); }

int ScreenCapturerWin::Init(const int fps, cb_desktop_data cb) {
  fps_ = fps;
  cb_orig_ = cb;
  cb_ = [this](unsigned char* data, int size, int w, int h,
               const char* display_name) {
    std::string mapped_name;
    {
      std::lock_guard<std::mutex> lock(alias_mutex_);
      auto it = label_alias_.find(display_name);
      if (it != label_alias_.end())
        mapped_name = it->second;
      else
        mapped_name = display_name;
    }
    {
      std::lock_guard<std::mutex> lock(alias_mutex_);
      if (canonical_labels_.find(mapped_name) == canonical_labels_.end()) {
        return;
      }
    }
    if (cb_orig_) cb_orig_(data, size, w, h, mapped_name.c_str());
  };

  int ret = -1;

  impl_ = std::make_unique<ScreenCapturerWgc>();
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using WGC");
    BuildCanonicalFromImpl();
    return 0;
  }

  LOG_WARN("Windows capturer: WGC init failed (ret={}), try DXGI", ret);
  impl_.reset();

  impl_ = std::make_unique<ScreenCapturerDxgi>();
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using DXGI Desktop Duplication");
    BuildCanonicalFromImpl();
    return 0;
  }

  LOG_WARN("Windows capturer: DXGI init failed (ret={}), fallback to GDI", ret);
  impl_.reset();

  impl_ = std::make_unique<ScreenCapturerGdi>();
  ret = impl_->Init(fps_, cb_);
  if (ret == 0) {
    LOG_INFO("Windows capturer: using GDI BitBlt");
    BuildCanonicalFromImpl();
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
  {
    std::lock_guard<std::mutex> lock(alias_mutex_);
    label_alias_.clear();
    handle_to_canonical_.clear();
    canonical_labels_.clear();
  }
  return 0;
}

int ScreenCapturerWin::Start(bool show_cursor) {
  if (!impl_) return -1;
  int ret = impl_->Start(show_cursor);
  if (ret == 0) return 0;

  LOG_WARN("Windows capturer: Start failed (ret={}), trying fallback", ret);

  auto try_init_start = [&](std::unique_ptr<ScreenCapturer> cand) -> bool {
    int r = cand->Init(fps_, cb_);
    if (r != 0) return false;
    int s = cand->Start(show_cursor);
    if (s == 0) {
      impl_ = std::move(cand);
      RebuildAliasesFromImpl();
      return true;
    }
    return false;
  };

  if (dynamic_cast<ScreenCapturerWgc*>(impl_.get())) {
    if (try_init_start(std::make_unique<ScreenCapturerDxgi>())) {
      LOG_INFO("Windows capturer: fallback to DXGI");
      return 0;
    }
    if (try_init_start(std::make_unique<ScreenCapturerGdi>())) {
      LOG_INFO("Windows capturer: fallback to GDI");
      return 0;
    }
  } else if (dynamic_cast<ScreenCapturerDxgi*>(impl_.get())) {
    if (try_init_start(std::make_unique<ScreenCapturerGdi>())) {
      LOG_INFO("Windows capturer: fallback to GDI");
      return 0;
    }
  }

  LOG_ERROR("Windows capturer: all fallbacks failed to start");
  return ret;
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

int ScreenCapturerWin::ResetToInitialMonitor() {
  if (!impl_) return -1;
  return impl_->ResetToInitialMonitor();
}

std::vector<DisplayInfo> ScreenCapturerWin::GetDisplayInfoList() {
  if (!impl_) return {};
  return impl_->GetDisplayInfoList();
}

void ScreenCapturerWin::BuildCanonicalFromImpl() {
  std::lock_guard<std::mutex> lock(alias_mutex_);
  handle_to_canonical_.clear();
  label_alias_.clear();
  canonical_displays_ = impl_->GetDisplayInfoList();
  canonical_labels_.clear();
  for (const auto& di : canonical_displays_) {
    handle_to_canonical_[di.handle] = di.name;
    canonical_labels_.insert(di.name);
  }
}

void ScreenCapturerWin::RebuildAliasesFromImpl() {
  std::lock_guard<std::mutex> lock(alias_mutex_);
  label_alias_.clear();
  auto current = impl_->GetDisplayInfoList();
  auto similar = [&](const DisplayInfo& a, const DisplayInfo& b) {
    int dl = std::abs(a.left - b.left);
    int dt = std::abs(a.top - b.top);
    int dw = std::abs(a.width - b.width);
    int dh = std::abs(a.height - b.height);
    return dl <= 10 && dt <= 10 && dw <= 20 && dh <= 20;
  };
  for (const auto& di : current) {
    std::string canonical;
    auto it = handle_to_canonical_.find(di.handle);
    if (it != handle_to_canonical_.end()) {
      canonical = it->second;
    } else {
      for (const auto& c : canonical_displays_) {
        if (similar(di, c) || (di.is_primary && c.is_primary)) {
          canonical = c.name;
          break;
        }
      }
    }
    if (!canonical.empty() && canonical != di.name) {
      label_alias_[di.name] = canonical;
    }
  }
}

}  // namespace crossdesk