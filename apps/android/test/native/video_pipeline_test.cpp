#include "latest_frame_worker.h"
#include "video_timing.h"
#include "video_picture.h"
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <vector>

int main() {
  using namespace crossdesk::android_controller;
  using namespace std::chrono_literals;
  assert(VideoLatencyMilliseconds(1'000'000, 1'020'000, 1'050'000) == 50);
  // Reproduce a valid frame delayed in the local decode/Surface queue.
  // The old submission-time five-second cutoff hid both of these samples.
  assert(VideoLatencyMilliseconds(1'000'000, 1'020'000, 7'000'000) == 6000);
  assert(VideoLatencyMilliseconds(1'000'000, 1'020'000, 31'000'000) == 30000);
  assert(VideoLatencyMilliseconds(0, 1'020'000, 7'000'000) < 0);
  assert(VideoLatencyMilliseconds(1'030'000, 1'020'000, 7'000'000) < 0);
  assert(VideoLatencyMilliseconds(1'000'000, 6'000'001, 7'000'000) < 0);
  assert(VideoLatencyMilliseconds(1'000'000, 1'020'000, 1'010'000) < 0);
  assert(VideoLatencyMilliseconds(1'000'000, 1'020'000, -1) < 0);

  struct Owner { int references = 1; uint8_t pixels[12]{}; } owner;
  MiniRtcNativeVideoFrame native{};
  native.struct_size = sizeof(native); native.type = MiniRtcNativeVideoFrameCpuNv12;
  native.width = native.height = 2; native.owner = &owner;
  native.retain = [](void* p) { ++static_cast<Owner*>(p)->references; };
  native.release = [](void* p) { --static_cast<Owner*>(p)->references; };
  native.payload.cpu_nv12 = {owner.pixels, owner.pixels + 8, 4, 4};
  MiniRtcVideoFrame source{}; source.width = source.height = 2;
  source.native_frame = &native; source.captured_timestamp = 100; source.received_timestamp = 120;
  {
    auto retained = VideoPicture::RetainOrCopy(source, 3);
    assert(retained && owner.references == 2 && retained->pixels.empty());
    assert(retained->Y() == owner.pixels && retained->UV() == owner.pixels + 8);
    assert(retained->YStride() == 4 && retained->UVStride() == 4);
    assert(retained->captured_us == 100 && retained->received_us == 120 && retained->generation == 3);
    auto moved = std::move(*retained); retained.reset(); assert(owner.references == 2);
  }
  assert(owner.references == 1);
  uint8_t borrowed[6]{1, 2, 3, 4, 5, 6};
  source.native_frame = nullptr; source.data = reinterpret_cast<const char*>(borrowed); source.size = 6;
  auto copied = VideoPicture::RetainOrCopy(source, 4); borrowed[0] = 0;
  assert(copied && copied->Y()[0] == 1 && copied->UV()[0] == 5 && copied->YStride() == 2);
  source.size = 5; assert(!VideoPicture::RetainOrCopy(source, 4));
  source.width = 3; assert(!VideoPicture::RetainOrCopy(source, 4));

  std::promise<void> started, release, finished;
  auto released = release.get_future();
  std::vector<int> rendered;
  LatestFrameWorker<std::unique_ptr<int>> worker([&](std::unique_ptr<int> value) {
    rendered.push_back(*value);
    if (*value == 1) { started.set_value(); released.wait(); }
    if (*value == 4) finished.set_value();
  });
  assert(worker.Submit(std::make_unique<int>(1)));
  assert(started.get_future().wait_for(2s) == std::future_status::ready);
  // A blocked Surface must not block frame submission; obsolete frames 2/3
  // must not be displayed after it recovers, even with move-only ownership.
  assert(worker.Submit(std::make_unique<int>(2)));
  assert(worker.Submit(std::make_unique<int>(3)));
  assert(worker.Submit(std::make_unique<int>(4)));
  release.set_value();
  assert(finished.get_future().wait_for(2s) == std::future_status::ready);
  worker.Stop();
  assert((rendered == std::vector<int>{1, 4}));
  assert(!worker.Submit(std::make_unique<int>(5)));
  worker.Stop();

  std::promise<void> first_started, first_release;
  auto first_released = first_release.get_future();
  int count = 0;
  LatestFrameWorker<int> cleared([&](int value) {
    ++count; assert(value == 1); first_started.set_value(); first_released.wait();
  });
  cleared.Submit(1);
  assert(first_started.get_future().wait_for(2s) == std::future_status::ready);
  cleared.Submit(2);
  cleared.Clear(); // Surface replacement/display switch discards pending pixels.
  first_release.set_value();
  cleared.Stop();
  assert(count == 1);
}
