#include "speaker_capturer_linux.h"

#include <pulse/error.h>
#include <pulse/introspect.h>

#include <condition_variable>
#include <iostream>
#include <thread>

#include "rd_log.h"

namespace crossdesk {

constexpr int kSampleRate = 48000;
constexpr pa_sample_format_t kFormat = PA_SAMPLE_S16LE;
constexpr int kChannels = 1;
constexpr size_t kFrameSizeBytes = 480 * sizeof(int16_t);

SpeakerCapturerLinux::SpeakerCapturerLinux()
    : inited_(false), paused_(false), stop_flag_(false) {}
SpeakerCapturerLinux::~SpeakerCapturerLinux() {
  Stop();
  Destroy();
}

int SpeakerCapturerLinux::Init(speaker_data_cb cb) {
  if (inited_) return 0;
  cb_ = cb;
  inited_ = true;
  return 0;
}

int SpeakerCapturerLinux::Destroy() {
  inited_ = false;
  return 0;
}

std::string SpeakerCapturerLinux::GetDefaultMonitorSourceName() {
  std::string monitor_name;
  std::mutex mtx;
  std::condition_variable cv;
  bool ready = false;

  pa_mainloop* mainloop = pa_mainloop_new();
  pa_mainloop_api* api = pa_mainloop_get_api(mainloop);
  pa_context* context = pa_context_new(api, "GetMonitor");

  struct CallbackState {
    std::string* name;
    std::mutex* mtx;
    std::condition_variable* cv;
    bool* ready;
  } state{&monitor_name, &mtx, &cv, &ready};

  pa_context_set_state_callback(
      context,
      [](pa_context* c, void* userdata) {
        auto* state = static_cast<CallbackState*>(userdata);
        if (pa_context_get_state(c) == PA_CONTEXT_READY) {
          pa_operation* operation = pa_context_get_server_info(
              c,
              [](pa_context*, const pa_server_info* info, void* userdata) {
                auto* state = static_cast<CallbackState*>(userdata);
                if (info && info->default_sink_name) {
                  *(state->name) =
                      std::string(info->default_sink_name) + ".monitor";
                }
                {
                  std::lock_guard<std::mutex> lock(*(state->mtx));
                  *(state->ready) = true;
                }
                state->cv->notify_one();
              },
              userdata);
          if (operation) {
            pa_operation_unref(operation);
          }
        }
      },
      &state);

  pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

  std::thread loop_thread([&]() {
    int ret = 0;
    pa_mainloop_run(mainloop, &ret);
  });

  {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return ready; });
  }

  pa_context_disconnect(context);
  pa_context_unref(context);
  pa_mainloop_quit(mainloop, 0);
  loop_thread.join();
  pa_mainloop_free(mainloop);

  return monitor_name;
}

int SpeakerCapturerLinux::Start() {
  if (!inited_ || mainloop_thread_.joinable()) return -1;
  stop_flag_ = false;

  mainloop_thread_ = std::thread([this]() {
    std::string monitor_name = GetDefaultMonitorSourceName();
    if (monitor_name.empty()) {
      LOG_ERROR("Failed to get monitor source");
      return;
    }

    mainloop_ = pa_threaded_mainloop_new();
    pa_mainloop_api* api = pa_threaded_mainloop_get_api(mainloop_);
    context_ = pa_context_new(api, "SpeakerCapturer");

    pa_context_set_state_callback(
        context_,
        [](pa_context* c, void* userdata) {
          auto self = static_cast<SpeakerCapturerLinux*>(userdata);
          pa_context_state_t state = pa_context_get_state(c);
          if (state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED ||
              state == PA_CONTEXT_TERMINATED) {
            pa_threaded_mainloop_signal(self->mainloop_, 0);
          }
        },
        this);

    if (pa_threaded_mainloop_start(mainloop_) < 0) {
      LOG_ERROR("Failed to start mainloop");
      Cleanup();
      return;
    }

    pa_threaded_mainloop_lock(mainloop_);

    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) <
        0) {
      LOG_ERROR("Failed to connect context");
      pa_threaded_mainloop_unlock(mainloop_);
      Cleanup();
      return;
    }

    while (true) {
      pa_context_state_t state = pa_context_get_state(context_);
      if (state == PA_CONTEXT_READY) break;
      if (!PA_CONTEXT_IS_GOOD(state) || stop_flag_) {
        pa_threaded_mainloop_unlock(mainloop_);
        Cleanup();
        return;
      }
      pa_threaded_mainloop_wait(mainloop_);
    }

    pa_sample_spec ss = {kFormat, kSampleRate, kChannels};
    stream_ = pa_stream_new(context_, "Capture", &ss, nullptr);

    pa_stream_set_read_callback(
        stream_,
        [](pa_stream* s, size_t len, void* u) {
          auto self = static_cast<SpeakerCapturerLinux*>(u);
          if (self->paused_ || self->stop_flag_) return;

          const void* data = nullptr;
          if (pa_stream_peek(s, &data, &len) < 0 || !data) return;

          const uint8_t* p = static_cast<const uint8_t*>(data);
          self->frame_cache_.insert(self->frame_cache_.end(), p, p + len);

          while (self->frame_cache_.size() >= kFrameSizeBytes) {
            std::vector<uint8_t> temp_frame(
                self->frame_cache_.begin(),
                self->frame_cache_.begin() + kFrameSizeBytes);
            self->cb_(temp_frame.data(), kFrameSizeBytes, "audio");
            self->frame_cache_.erase(
                self->frame_cache_.begin(),
                self->frame_cache_.begin() + kFrameSizeBytes);
          }

          pa_stream_drop(s);
        },
        this);

    pa_buffer_attr attr = {.maxlength = (uint32_t)-1,
                           .tlength = 0,
                           .prebuf = 0,
                           .minreq = 0,
                           .fragsize = (uint32_t)kFrameSizeBytes};

    if (pa_stream_connect_record(stream_, monitor_name.c_str(), &attr,
                                 PA_STREAM_ADJUST_LATENCY) < 0) {
      LOG_ERROR("Failed to connect stream");
      pa_threaded_mainloop_unlock(mainloop_);
      Cleanup();
      return;
    }

    while (true) {
      pa_stream_state_t s = pa_stream_get_state(stream_);
      if (s == PA_STREAM_READY) break;
      if (!PA_STREAM_IS_GOOD(s) || stop_flag_) {
        pa_threaded_mainloop_unlock(mainloop_);
        Cleanup();
        return;
      }
      pa_threaded_mainloop_wait(mainloop_);
    }

    pa_threaded_mainloop_unlock(mainloop_);

    while (!stop_flag_)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  });

  return 0;
}

int SpeakerCapturerLinux::Stop() {
  stop_flag_ = true;

  if (mainloop_) {
    pa_threaded_mainloop_lock(mainloop_);
    pa_threaded_mainloop_signal(mainloop_, 0);
    pa_threaded_mainloop_unlock(mainloop_);
  }

  if (mainloop_thread_.joinable()) {
    mainloop_thread_.join();
  }

  Cleanup();
  return 0;
}

void SpeakerCapturerLinux::Cleanup() {
  if (mainloop_) {
    pa_threaded_mainloop_stop(mainloop_);
    pa_threaded_mainloop_lock(mainloop_);

    if (stream_) {
      pa_stream_disconnect(stream_);
      pa_stream_unref(stream_);
      stream_ = nullptr;
    }

    if (context_) {
      pa_context_disconnect(context_);
      pa_context_unref(context_);
      context_ = nullptr;
    }

    pa_threaded_mainloop_unlock(mainloop_);
    pa_threaded_mainloop_free(mainloop_);
    mainloop_ = nullptr;
  }

  frame_cache_.clear();
}

int SpeakerCapturerLinux::Pause() {
  paused_ = true;
  return 0;
}

int SpeakerCapturerLinux::Resume() {
  paused_ = false;
  return 0;
}
}  // namespace crossdesk