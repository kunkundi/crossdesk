#include "agent.h"
#include "terminal_io.h"
#include "video_server.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <deque>
#include <frame_image.h>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
#include <terminal_renderer.h>

namespace crossdesk::cli {
namespace {
using nlohmann::json;
using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;
constexpr size_t kMaxResponse = 4 * 1024 * 1024;
void Fields(const json &j, std::initializer_list<const char *> names) {
  if (!j.is_object())
    throw std::invalid_argument("INVALID_REQUEST");
  for (auto it = j.begin(); it != j.end(); ++it)
    if (std::none_of(names.begin(), names.end(),
                     [&](const char *s) { return it.key() == s; }))
      throw std::invalid_argument("UNKNOWN_FIELD");
}
int Number(const json &j, const char *key, int low, int high, int fallback) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_number_integer())
    throw std::invalid_argument("INVALID_REQUEST");
  const auto n = j[key].get<int64_t>();
  if (n < low || n > high)
    throw std::invalid_argument("INVALID_REQUEST");
  return int(n);
}
uint64_t Sequence(const json &j, const char *key, uint64_t fallback = 0) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_number_unsigned())
    throw std::invalid_argument("INVALID_REQUEST");
  return j[key].get<uint64_t>();
}
bool Flag(const json &j, const char *key, bool fallback = false) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_boolean())
    throw std::invalid_argument("INVALID_REQUEST");
  return j[key].get<bool>();
}
std::string String(const json &j, const char *key, size_t limit = 128) {
  if (!j.contains(key) || !j[key].is_string())
    throw std::invalid_argument("INVALID_REQUEST");
  auto s = j[key].get<std::string>();
  if (s.empty() || s.size() > limit || s.find('\0') != std::string::npos)
    throw std::invalid_argument("INVALID_REQUEST");
  return s;
}
int64_t Age(Clock::time_point queued) {
  return std::chrono::duration_cast<Ms>(Clock::now() - queued).count();
}
class Responses {
public:
  explicit Responses(int fd) : fd_(fd), mode_(fd, true) {}
  void Put(json j) {
    auto line = j.dump(-1, ' ', false, json::error_handler_t::replace) + '\n';
    if (line.size() > kMaxResponse || queue_.size() >= 64 ||
        bytes_ + line.size() > 8 * 1024 * 1024)
      throw std::runtime_error("OUTPUT_BACKPRESSURE");
    if (queue_.empty())
      progressed_ = Clock::now();
    bytes_ += line.size();
    queue_.push_back(std::move(line));
  }
  bool Empty() const { return queue_.empty(); }
  void Pump() {
    if (Empty())
      return;
    const auto &s = queue_.front();
    auto n = Write(fd_, s.data() + offset_,
                   std::min(size_t(16384), s.size() - offset_));
    if (n > 0) {
      offset_ += n;
      bytes_ -= n;
      progressed_ = Clock::now();
      if (offset_ == s.size()) {
        queue_.pop_front();
        offset_ = 0;
      }
    } else if (n < 0 && errno != EAGAIN && errno != EINTR)
      throw std::runtime_error("OUTPUT_CLOSED");
    if (Clock::now() - progressed_ > std::chrono::seconds(10))
      throw std::runtime_error("OUTPUT_TIMEOUT");
  }

private:
  int fd_;
  NonBlocking mode_;
  std::deque<std::string> queue_;
  size_t offset_ = 0, bytes_ = 0;
  Clock::time_point progressed_ = Clock::now();
};
struct Observation {
  std::string id;
  uint64_t generation, epoch;
  int width, height, source_width, source_height;
  ImageRegion region;
  Clock::time_point created;
};
struct Capture {
  json id, request;
  bool preview = false;
  Clock::time_point deadline;
};
struct Encoding {
  Capture capture;
  std::shared_ptr<const DecodedFrame> frame;
  uint64_t epoch;
  bool cancelled = false;
  std::future<FrameImage> result;
};
struct TerminalEncoding {
  json id;
  uint64_t generation, epoch;
  Clock::time_point queued;
  std::future<terminal::Grid> result;
};
struct Step {
  int delay = 0, kind = 0, key = 0, flag = 0, wheel = 0;
  float x = 0, y = 0;
  bool down = false;
  std::string text;
};
struct RunningAction {
  json id;
  std::string operation;
  uint64_t generation = 0, epoch = 0;
  std::vector<Step> steps;
  size_t next = 0;
  bool submitted = false;
  Clock::time_point started;
};
struct Record {
  std::string operation, arguments;
  json result;
  Clock::time_point created;
};
int KeyCode(const std::string &key) {
  if (key.size() == 1 &&
      ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9')))
    return key[0];
  static const std::map<std::string, int> keys = {
      {"BACKSPACE", 8}, {"TAB", 9},    {"ENTER", 13},   {"SHIFT", 16},
      {"CTRL", 17},     {"ALT", 18},   {"PAUSE", 19},   {"CAPS_LOCK", 20},
      {"ESC", 27},      {"SPACE", 32}, {"PAGE_UP", 33}, {"PAGE_DOWN", 34},
      {"END", 35},      {"HOME", 36},  {"LEFT", 37},    {"UP", 38},
      {"RIGHT", 39},    {"DOWN", 40},  {"INSERT", 45},  {"DELETE", 46},
      {"META", 91},     {"F1", 112},   {"F2", 113},     {"F3", 114},
      {"F4", 115},      {"F5", 116},   {"F6", 117},     {"F7", 118},
      {"F8", 119},      {"F9", 120},   {"F10", 121},    {"F11", 122},
      {"F12", 123}};
  auto it = keys.find(key);
  if (it == keys.end())
    throw std::invalid_argument("INVALID_KEY");
  return it->second;
}
class Automation {
public:
  Automation(int output, SessionOptions options, int password, bool video)
      : output_(output), options_(std::move(options)), password_fd_(password),
        video_(video ? std::make_unique<VideoServer>() : nullptr) {}
  ~Automation() { session_.Disconnect(); }
  int Run() {
    NonBlocking input(0);
    std::string buffer;
    bool eof = false, quit = false;
    auto until = Clock::time_point::max();
    while (!interrupted) {
      Tick();
      output_.Pump();
      PumpVideo();
      if (!quit) {
        char bytes[8192];
        const auto n = Read(0, bytes, sizeof(bytes));
        if (n > 0)
          buffer.append(bytes, size_t(n));
        else if (!n)
          eof = true;
        else if (errno != EAGAIN && errno != EINTR)
          eof = true;
        if (buffer.size() > 65536) {
          Error(nullptr, "MESSAGE_TOO_LARGE");
          quit = true;
        }
        for (int count = 0; count < 16 && !quit; ++count) {
          const auto end = buffer.find('\n');
          if (end == std::string::npos)
            break;
          auto request = ParseBoundedJson(buffer.substr(0, end));
          buffer.erase(0, end + 1);
          quit = Handle(request);
        }
        if (eof && buffer.find('\n') == std::string::npos) {
          if (!buffer.empty())
            Error(nullptr, "INCOMPLETE_MESSAGE");
          quit = true;
        }
      }
      if (quit && until == Clock::time_point::max()) {
        Stop("CANCELLED");
        session_.Disconnect();
        PumpVideo();
        until = Clock::now() + std::chrono::seconds(2);
      }
      if (quit && (output_.Empty() || Clock::now() >= until))
        break;
      WaitIo(quit ? -1 : 0, output_.Empty() ? -1 : output_fd(), 5);
    }
    return interrupted ? 130 : 0;
  }
  void SetOutputFd(int fd) { output_fd_ = fd; }

private:
  int output_fd() const { return output_fd_; }
  json Status() const {
    auto result = session_.Status();
    result["video"] = {{"decode_path", decode_path_}, {"decoder", decoder_}};
    result["video_output"]["enabled"] = video_ && video_->Enabled();
    result["video_output"]["protocol"] =
        decode_path_ == "minirtc" ? "nv12/1" : "h264-annexb/1";
    if (video_) result["video_output"].update(video_->Status());
    if (decode_path_ == "minirtc") {
      result["video_output"]["received"] = frame_ ? frame_->frame_id : 0;
      result["video_output"]["waiting_keyframe"] = false;
      result["video_output"].erase("has_sps");
      result["video_output"].erase("has_pps");
    }
    result["transport_state"] = result["state"];
    result["geometry_epoch"] = epoch_;
    result["cursor"] = Cursor();
    result["human_paused"] = paused_;
    result["control_allowed"] = allowed_;
    result["frame_age_ms"] = frame_ ? json(Age(frame_->queued)) : json(nullptr);
    result["source_size"] =
        frame_ ? json{{"width", frame_->width}, {"height", frame_->height}}
               : json(nullptr);
    if (session_.Connected())
      result["state"] = frame_ && frame_->generation == session_.Generation()
                            ? "ready"
                            : "waiting_frame";
    return result;
  }
  json Cursor() const {
    const bool visible = cursor_visible_ && frame_ && session_.Connected() &&
        frame_->generation == session_.Generation() && allowed_ && !paused_ &&
        session_.ControlEnabled();
    return {{"type", "cursor"}, {"visible", visible},
            {"x", cursor_x_}, {"y", cursor_y_},
            {"generation", session_.Generation()}, {"geometry_epoch", epoch_},
            {"width", frame_ ? frame_->width : 0},
            {"height", frame_ ? frame_->height : 0}};
  }
  void PumpVideo() {
    if (!video_) return;
    const auto generation = session_.Generation();
    const bool connected = session_.Connected();
    const bool decoded = decode_path_ == "minirtc";
    if (generation != video_generation_ || connected != video_connected_) {
      video_->Bind(generation, decoded, connected);
      video_generation_ = generation;
      video_connected_ = connected;
      last_video_frame_ = 0;
    }
    const auto cursor = Cursor();
    if (cursor != video_cursor_) {
      video_->Cursor(cursor);
      video_cursor_ = cursor;
    }
    const bool enabled = video_->Enabled();
    const bool key = video_->TakeKeyframeRequest();
    if (enabled != video_enabled_ || key) {
      session_.SetVideoOutput(enabled && !decoded, key && !decoded);
      video_enabled_ = enabled;
      last_video_frame_ = 0;
    }
    if (!enabled) return;
    if (decoded) {
      if (frame_ && frame_->generation == generation && Age(frame_->queued) <= 2000 &&
          frame_->frame_id != last_video_frame_) {
        video_->Submit(frame_);
        last_video_frame_ = frame_->frame_id;
      }
    } else {
      for (int count = 0; count < 8; ++count) {
        auto frame = session_.TakeEncodedVideo();
        if (!frame) break;
        video_->Submit(std::move(*frame));
      }
    }
  }
  void Reply(const json &id, json value) {
    output_.Put({{"id", id}, {"ok", true}, {"result", std::move(value)}});
  }
  void Error(const json &id, const std::string &code,
             bool side_effect = false) {
    output_.Put({{"id", id},
                 {"ok", false},
                 {"error",
                  {{"code", code},
                   {"retryable", !side_effect && (code == "FRAME_TIMEOUT" ||
                                                  code == "BUSY")},
                   {"side_effect_possible", side_effect}}}});
  }
  void Release() {
    const bool enabled = session_.Status()["control"];
    session_.SetControl(false);
    if (enabled && allowed_ && !paused_ && session_.Connected())
      session_.SetControl(true);
  }
  void FinishAction(const char *error = nullptr) {
    if (!action_)
      return;
    Release();
    json result = {
        {"status", error ? (action_->submitted ? "outcome_unknown"
                                               : "failed_before_submit")
                         : "submitted"},
        {"operation_id", action_->operation},
        {"generation", action_->generation},
        {"after_frame_id", frame_ ? frame_->frame_id : 0},
        {"side_effect_possible", action_->submitted},
        {"retryable", false}};
    if (error)
      result["error"] = error;
    for (auto &record : records_)
      if (record.operation == action_->operation) {
        record.result = result;
        break;
      }
    Reply(action_->id, result);
    action_.reset();
  }
  void Stop(const char *code) {
    FinishAction(code);
    for (const auto &capture : captures_)
      Error(capture.id, code);
    captures_.clear();
    if (encoding_ && !encoding_->cancelled) {
      Error(encoding_->capture.id, code);
      encoding_->cancelled = true;
    }
    observations_.clear();
    preview_ = json();
  }
  void Tick() {
    session_.Tick();
    if (auto f = session_.TakeFrame()) {
      if (!frame_ || f->generation != frame_->generation ||
          f->width != frame_->width || f->height != frame_->height) {
        ++epoch_;
        cursor_visible_ = false;
        observations_.clear();
        preview_ = json();
      }
      frame_ = std::make_shared<DecodedFrame>(std::move(*f));
    }
    if (frame_ && frame_->generation != session_.Generation()) {
      frame_.reset();
      ++epoch_;
      cursor_visible_ = false;
      observations_.clear();
      preview_ = json();
    }
    if (action_) {
      if (!session_.Connected() || !frame_ || paused_ || !allowed_ ||
          action_->generation != session_.Generation() ||
          action_->epoch != epoch_)
        FinishAction("SESSION_CHANGED");
      while (action_ && action_->next < action_->steps.size() &&
             Age(action_->started) >= action_->steps[action_->next].delay) {
        const auto &step = action_->steps[action_->next++];
        action_->submitted =
            true; // A failed send may still have reached the peer.
        const bool sent =
            step.kind == 0
                ? session_.Pointer(step.x, step.y, step.flag, step.wheel)
            : step.kind == 1 ? session_.Key(step.key, step.down)
                             : session_.Text(step.text);
        if (!sent) {
          FinishAction("ACTION_OUTCOME_UNKNOWN");
          break;
        }
        if (step.kind == 0) {
          cursor_x_ = step.x;
          cursor_y_ = step.y;
          cursor_visible_ = true;
        }
        action_->submitted = true;
      }
      if (action_ && action_->next == action_->steps.size())
        FinishAction();
    }
    if (terminal_encoding_ &&
        terminal_encoding_->result.wait_for(Ms(0)) == std::future_status::ready) {
      auto job = std::move(terminal_encoding_);
      try {
        auto grid = job->result.get();
        if (!session_.Connected() || job->generation != session_.Generation() ||
            job->epoch != epoch_)
          Error(job->id, "STALE_OBSERVATION");
        else if (Age(job->queued) > 2000)
          Error(job->id, "FRAME_TIMEOUT");
        else {
          json cursor = nullptr;
          if (Cursor()["visible"].get<bool>()) {
            const int x = grid.content.x + std::clamp(
                int(cursor_x_ * grid.content.width), 0, grid.content.width - 1);
            const int y = grid.content.y + std::clamp(
                int(cursor_y_ * grid.content.height), 0, grid.content.height - 1);
            cursor = {{"column", x / grid.samples_x}, {"row", y / grid.samples_y}};
          }
          Reply(job->id, {{"columns", grid.viewport.columns}, {"rows", grid.viewport.rows},
                          {"lines", terminal::Lines(grid)}, {"cursor", cursor},
                          {"generation", job->generation}, {"geometry_epoch", job->epoch}});
        }
      } catch (...) { Error(job->id, "FRAME_RENDER_FAILED"); }
    }
    if (encoding_ &&
        encoding_->result.wait_for(Ms(0)) == std::future_status::ready) {
      auto job = std::move(encoding_);
      try {
        auto image = job->result.get();
        if (!job->cancelled) {
          if (!frame_ || job->epoch != epoch_ ||
              job->frame->generation != session_.Generation())
            Error(job->capture.id, "STALE_OBSERVATION");
          else if (Age(job->frame->queued) > 2000 ||
                   Clock::now() > job->capture.deadline)
            Error(job->capture.id, "FRAME_TIMEOUT");
          else {
            const auto &f = *job->frame;
            json meta = {
                {"generation", f.generation},
                {"geometry_epoch", job->epoch},
                {"frame_id", f.frame_id},
                {"display_index", session_.Status()["display"]},
                {"source_size", {{"width", f.width}, {"height", f.height}}},
                {"source_rect",
                 {{"x", image.region.x},
                  {"y", image.region.y},
                  {"width", image.region.width},
                  {"height", image.region.height}}},
                {"image_size",
                 {{"width", image.width}, {"height", image.height}}},
                {"frame_age_ms", Age(f.queued)}};
            json result = {{"mime_type", "image/png"},
                           {"data", ImageBase64(image.png)},
                           {"metadata", meta}};
            if (job->capture.preview) {
              preview_ = result;
              preview_time_ = f.queued;
              preview_edge_ =
                  Number(job->capture.request, "max_edge", 64, 960, 960);
            } else {
              const auto id = "obs_" + std::to_string(f.generation) + "_" +
                              std::to_string(++observation_sequence_);
              result["metadata"]["observation_id"] = id;
              if (observations_.size() >= 32)
                observations_.pop_front();
              observations_.push_back(
                  {id, f.generation, job->epoch, image.width, image.height,
                   int(f.width), int(f.height), image.region, Clock::now()});
            }
            Reply(job->capture.id, std::move(result));
          }
        }
      } catch (...) {
        if (!job->cancelled)
          Error(job->capture.id, "IMAGE_ENCODING_FAILED");
      }
    }
    for (auto it = captures_.begin(); it != captures_.end();) {
      if (it->request.contains("generation") &&
          Sequence(it->request, "generation") != session_.Generation()) {
        Error(it->id, "STALE_OBSERVATION");
        it = captures_.erase(it);
      } else if (Clock::now() > it->deadline || session_.Finished()) {
        Error(it->id, session_.Finished() ? "NOT_READY" : "FRAME_TIMEOUT");
        it = captures_.erase(it);
      } else
        ++it;
    }
    if (!encoding_ && !captures_.empty() && frame_ &&
        Age(frame_->queued) <= 2000) {
      auto it = std::find_if(
          captures_.begin(), captures_.end(), [&](const Capture &c) {
            return !c.request.contains("after_frame_id") ||
                   frame_->frame_id > Sequence(c.request, "after_frame_id");
          });
      if (it != captures_.end()) {
        auto capture = *it;
        const auto capture_id = capture.id;
        captures_.erase(it);
        try {
          Encode(std::move(capture));
        } catch (...) {
          Error(capture_id, "INVALID_REGION");
        }
      }
    }
  }
  void Encode(Capture capture) {
    ImageRegion region;
    if (capture.request.contains("region")) {
      const auto &r = capture.request["region"];
      Fields(r, {"x", "y", "width", "height"});
      region = {Number(r, "x", 0, int(frame_->width) - 1, -1),
                Number(r, "y", 0, int(frame_->height) - 1, -1),
                Number(r, "width", 1, frame_->width, -1),
                Number(r, "height", 1, frame_->height, -1)};
      if (region.x < 0 || region.y < 0 || region.width < 1 ||
          region.height < 1 || region.x > int(frame_->width) - region.width ||
          region.y > int(frame_->height) - region.height)
        throw std::invalid_argument("INVALID_REGION");
    }
    const int edge =
        Number(capture.request, "max_edge", 64, capture.preview ? 960 : 2048,
               capture.preview ? 960 : 1280);
    auto job = std::make_unique<Encoding>();
    job->capture = capture;
    job->frame = frame_;
    job->epoch = epoch_;
    job->result = std::async(std::launch::async, [frame = frame_, region, edge,
                                                  preview = capture.preview] {
      return EncodeFrameImage(*frame, region, edge,
                              preview ? 256 * 1024 : 2 * 1024 * 1024);
    });
    encoding_ = std::move(job);
  }
  void CaptureFrame(const json &j, const json &id, bool preview) {
    Fields(j, {"id", "method", "region", "max_edge", "after_frame_id",
               "generation", "timeout_ms"});
    if (!session_.Connected())
      throw std::invalid_argument("NOT_READY");
    Number(j, "max_edge", 64, preview ? 960 : 2048, preview ? 960 : 1280);
    if (preview && j.contains("region"))
      throw std::invalid_argument("INVALID_REQUEST");
    if (j.contains("after_frame_id") && !j.contains("generation"))
      throw std::invalid_argument("GENERATION_REQUIRED");
    Sequence(j, "after_frame_id");
    if (j.contains("generation") &&
        Sequence(j, "generation") != session_.Generation())
      throw std::invalid_argument("STALE_OBSERVATION");
    if (preview && frame_ && j.contains("after_frame_id") &&
        Sequence(j, "after_frame_id") >= frame_->frame_id) {
      Reply(id, {{"unchanged", true},
                 {"generation", frame_->generation},
                 {"frame_id", frame_->frame_id},
                 {"frame_age_ms", Age(frame_->queued)},
                 {"stale", Age(frame_->queued) > 2000}});
      return;
    }
    if (preview && frame_ && !preview_.is_null() &&
        Age(preview_time_) <= 2000 &&
        preview_edge_ == Number(j, "max_edge", 64, 960, 960) &&
        preview_["metadata"]["generation"] == frame_->generation &&
        preview_["metadata"]["frame_id"] == frame_->frame_id) {
      auto cached = preview_;
      cached["metadata"]["frame_age_ms"] = Age(preview_time_);
      Reply(id, cached);
      return;
    }
    if (captures_.size() >= 16)
      throw std::invalid_argument("BUSY");
    Capture c{id, j, preview,
              Clock::now() + Ms(Number(j, "timeout_ms", 0, 5000, 2000))};
    if (preview)
      captures_.push_back(c);
    else
      captures_.push_front(c);
  }
  const Observation &Observe(const json &j) {
    auto name = String(j, "observation_id");
    auto it = std::find_if(observations_.begin(), observations_.end(),
                           [&](const Observation &o) { return o.id == name; });
    if (it == observations_.end() || it->generation != session_.Generation() ||
        it->epoch != epoch_ || Age(it->created) > 30000)
      throw std::invalid_argument("STALE_OBSERVATION");
    return *it;
  }
  void StartAction(const json &j, const json &id) {
    Fields(j, {"id", "method", "operation_id", "observation_id", "action"});
    const auto operation = String(j, "operation_id");
    const auto arguments = json{
        {"observation_id", j.at("observation_id")},
        {"action",
         j.at("action")}}.dump();
    for (const auto &r : records_)
      if (r.operation == operation) {
        if (r.arguments != arguments)
          throw std::invalid_argument("OPERATION_ID_CONFLICT");
        Reply(id, r.result);
        return;
      }
    if (!allowed_ || paused_ || !session_.Status()["control"].get<bool>())
      throw std::invalid_argument("CONTROL_DENIED");
    if (!session_.Connected() || !frame_)
      throw std::invalid_argument("NOT_READY");
    if (action_)
      throw std::invalid_argument("BUSY");
    const auto &o = Observe(j);
    const auto &a = j.at("action");
    const auto type = String(a, "type", 32);
    auto running = std::make_unique<RunningAction>();
    running->id = id;
    running->operation = operation;
    running->generation = o.generation;
    running->epoch = o.epoch;
    auto point = [&](const json &p, int delay, int flag = 0, int wheel = 0) {
      auto coordinate = [&](const char *key, int size, int offset, int span,
                            int source) {
        if (!p.contains(key) || !p[key].is_number())
          throw std::invalid_argument("INVALID_COORDINATE");
        const auto value = p[key].get<double>();
        if (!std::isfinite(value) || value < 0 || value >= size)
          throw std::invalid_argument("INVALID_COORDINATE");
        return float(std::clamp((offset + (value + .5) * span / size) / source,
                                0.0, 1.0));
      };
      Step step;
      step.delay = delay;
      step.flag = flag;
      step.wheel = wheel;
      step.x =
          coordinate("x", o.width, o.region.x, o.region.width, o.source_width);
      step.y = coordinate("y", o.height, o.region.y, o.region.height,
                          o.source_height);
      running->steps.push_back(std::move(step));
    };
    auto key = [&](int code, bool down, int delay) {
      Step step;
      step.kind = 1;
      step.key = code;
      step.down = down;
      step.delay = delay;
      running->steps.push_back(std::move(step));
    };
    if (type == "move" || type == "click" || type == "double_click") {
      Fields(a, type == "move"
                    ? std::initializer_list<const char *>{"type", "x", "y"}
                    : std::initializer_list<const char *>{"type", "x", "y",
                                                          "button"});
      point(a, 0);
      if (type != "move") {
        const auto button =
            a.contains("button") ? String(a, "button", 8) : "left";
        const int flag = button == "left"     ? 1
                         : button == "right"  ? 3
                         : button == "middle" ? 5
                                              : -1;
        if (flag < 0)
          throw std::invalid_argument("INVALID_BUTTON");
        point(a, 10, flag);
        point(a, 40, flag + 1);
        if (type == "double_click") {
          point(a, 110, flag);
          point(a, 140, flag + 1);
        }
      }
    } else if (type == "scroll") {
      Fields(a, {"type", "x", "y", "direction", "ticks"});
      const auto direction = String(a, "direction", 8);
      if (direction != "up" && direction != "down" && direction != "left" &&
          direction != "right")
        throw std::invalid_argument("INVALID_DIRECTION");
      const int ticks = Number(a, "ticks", 1, 10, 1);
      point(a, 0);
      point(a, 10, direction == "left" || direction == "right" ? 8 : 7,
            (direction == "up" || direction == "right" ? 120 : -120) * ticks);
    } else if (type == "drag") {
      Fields(a, {"type", "path", "duration_ms"});
      const auto &path = a.at("path");
      if (!path.is_array() || path.size() < 2 || path.size() > 128)
        throw std::invalid_argument("INVALID_PATH");
      const int duration = Number(a, "duration_ms", 100, 2000, 500);
      for (const auto &p : path)
        Fields(p, {"x", "y"});
      point(path.front(), 0);
      point(path.front(), 10, 1);
      for (size_t i = 1; i < path.size(); ++i)
        point(path[i], 10 + int(i * duration / (path.size() - 1)));
      point(path.back(), duration + 30, 2);
    } else if (type == "key_press") {
      Fields(a, {"type", "key"});
      const auto code = KeyCode(String(a, "key", 16));
      key(code, true, 0);
      key(code, false, 40);
    } else if (type == "hotkey") {
      Fields(a, {"type", "keys"});
      const auto &keys = a.at("keys");
      if (!keys.is_array() || keys.size() < 2 || keys.size() > 6)
        throw std::invalid_argument("INVALID_KEYS");
      std::set<int> seen;
      std::vector<int> codes;
      for (const auto &item : keys) {
        if (!item.is_string())
          throw std::invalid_argument("INVALID_KEY");
        int code = KeyCode(item.get<std::string>());
        if (!seen.insert(code).second)
          throw std::invalid_argument("DUPLICATE_KEY");
        codes.push_back(code);
      }
      int delay = 0;
      for (int code : codes) {
        key(code, true, delay);
        delay += 15;
      }
      delay += 30;
      for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
        key(*it, false, delay);
        delay += 15;
      }
    } else if (type == "type_text") {
      Fields(a, {"type", "text"});
      if (!session_.Status()["text_input"].get<bool>())
        throw std::invalid_argument("TEXT_INPUT_UNSUPPORTED");
      Step step;
      step.kind = 2;
      step.text = String(a, "text", 16384);
      running->steps.push_back(std::move(step));
    } else
      throw std::invalid_argument("UNSUPPORTED_ACTION");
    while (records_.size() >= 128 ||
           (!records_.empty() && Age(records_.front().created) > 300000))
      records_.pop_front();
    records_.push_back(
        {operation,
         arguments,
         {{"status", "in_progress"}, {"operation_id", operation}},
         Clock::now()});
    running->started = Clock::now();
    action_ = std::move(running);
  }
  std::string Password() {
    if (password_fd_ < 3 || password_fd_ > 1024)
      throw std::invalid_argument("CREDENTIAL_CHANNEL_INVALID");
    std::string secret;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!interrupted && Clock::now() < deadline) {
      if (!WaitReadable(password_fd_, 10))
        continue;
      char c;
      const auto n = Read(password_fd_, &c, 1);
      if (!n || (n == 1 && c == '\n'))
        return secret;
      if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
          continue;
        throw std::invalid_argument("CREDENTIAL_CHANNEL_FAILED");
      }
      if (!c || secret.size() >= 256)
        throw std::invalid_argument("INVALID_CREDENTIAL");
      secret += c;
    }
    std::fill(secret.begin(), secret.end(), '\0');
    throw std::invalid_argument("CREDENTIAL_TIMEOUT");
  }
  bool Handle(const json &j) {
    json id = nullptr;
    try {
      if (!j.is_object() || !j.contains("id") || !j["id"].is_number_integer() ||
          j["id"].get<int64_t>() < 0)
        throw std::invalid_argument("INVALID_REQUEST");
      id = j["id"];
      const auto method = String(j, "method", 32);
      if (method == "hello") {
        Fields(j, {"id", "method", "protocol"});
        if (String(j, "protocol") != "crossdesk-automation/1")
          throw std::invalid_argument("VERSION_MISMATCH");
        hello_ = true;
        Reply(id, {{"protocol", "crossdesk-automation/1"},
                   {"image_formats", {"image/png"}},
                   {"video_protocols", {"h264-annexb/1", "nv12/1"}},
                   {"video_decoders", {"auto", "software"}},
                   {"video_transport", "loopback-websocket/1"},
                   {"terminal_protocol", "ansi/1"},
                   {"action_completion", "submitted"},
                   {"max_request_bytes", 65536},
                   {"max_response_bytes", kMaxResponse},
                   {"operation_cache_entries", 128},
                   {"operation_cache_ms", 300000},
                   {"human_pause", true}});
        return false;
      }
      if (!hello_)
        throw std::invalid_argument("HELLO_REQUIRED");
      if (method == "connect") {
        Fields(j, {"id", "method", "device", "display", "mode",
                    "decode_path", "decoder"});
        if (connected_once_)
          throw std::invalid_argument("NEW_PROCESS_REQUIRED");
        const auto device = String(j, "device", 9);
        if (device.size() != 9 ||
            device.find_first_not_of("0123456789") != std::string::npos)
          throw std::invalid_argument("INVALID_DEVICE");
        const auto mode = j.contains("mode") ? String(j, "mode", 8) : "observe";
        if (mode != "observe" && mode != "control")
          throw std::invalid_argument("INVALID_MODE");
        options_.display = Number(j, "display", 0, 63, 0);
        const auto path = j.contains("decode_path")
                              ? String(j, "decode_path", 16) : "browser";
        const auto decoder = j.contains("decoder")
                                 ? String(j, "decoder", 16) : "auto";
        if ((path != "browser" && path != "minirtc") ||
            (decoder != "auto" && decoder != "software"))
          throw std::invalid_argument("INVALID_VIDEO_CONFIG");
        decode_path_ = path;
        decoder_ = decoder;
        options_.hardware_acceleration = decoder != "software";
        auto password = Password();
        connected_once_ = true;
        paused_ = false;
        allowed_ = mode == "control";
        session_.Connect(options_, device, std::move(password));
        session_.SetControl(allowed_);
      } else if (method == "screenshot" || method == "preview") {
        CaptureFrame(j, id, method == "preview");
        return false;
      } else if (method == "action") {
        StartAction(j, id);
        return false;
      } else if (method == "terminal_frame") {
        Fields(j, {"id", "method", "columns", "rows", "colors", "cell_aspect"});
        if (terminal_encoding_) throw std::invalid_argument("BUSY");
        if (!session_.Connected() || !frame_ ||
            frame_->generation != session_.Generation() || Age(frame_->queued) > 2000)
          throw std::invalid_argument("NOT_READY");
        terminal::Viewport viewport;
        viewport.columns = Number(j, "columns", 1, 160, 80);
        viewport.rows = Number(j, "rows", 1, 60, 22);
        viewport.colors = Number(j, "colors", 8, 256, 24);
        if (viewport.colors != 8 && viewport.colors != 24 && viewport.colors != 256)
          throw std::invalid_argument("INVALID_REQUEST");
        viewport.cell_aspect = j.value("cell_aspect", .5);
        if (!std::isfinite(viewport.cell_aspect) || viewport.cell_aspect < .2 || viewport.cell_aspect > 2)
          throw std::invalid_argument("INVALID_REQUEST");
        viewport.detail = terminal::AnsiDetail::Quadrant;
        auto job = std::make_unique<TerminalEncoding>();
        job->id = id;
        job->generation = frame_->generation;
        job->epoch = epoch_;
        job->queued = frame_->queued;
        job->result = std::async(std::launch::async, [frame = frame_, viewport] {
          return terminal::Convert(*frame, viewport);
        });
        terminal_encoding_ = std::move(job);
        return false;
      } else if (method == "control") {
        Fields(j, {"id", "method", "enabled"});
        const bool enable = Flag(j, "enabled");
        if (enable && (!allowed_ || paused_))
          throw std::invalid_argument("CONTROL_DENIED");
        if (!enable)
          FinishAction("CONTROL_DISABLED");
        session_.SetControl(enable);
      } else if (method == "viewer_control") {
        Fields(j, {"id", "method", "operation"});
        const auto operation = String(j, "operation", 16);
        if (operation == "pause") {
          paused_ = true;
          FinishAction("USER_PAUSED");
          session_.SetControl(false);
        } else if (operation == "resume") {
          if (!allowed_)
            throw std::invalid_argument("CONTROL_DENIED");
          paused_ = false;
          session_.SetControl(true);
        } else if (operation == "disconnect") {
          Stop("CANCELLED");
          session_.Disconnect();
        } else
          throw std::invalid_argument("INVALID_REQUEST");
      } else if (method == "display") {
        Fields(j, {"id", "method", "index"});
        Stop("SESSION_CHANGED");
        if (!session_.SelectDisplay(Number(j, "index", 0, 63, -1)))
          throw std::invalid_argument("DISPLAY_UNAVAILABLE");
        frame_.reset();
        ++epoch_;
        cursor_visible_ = false;
      } else if (method == "cancel") {
        Fields(j, {"id", "method", "request_id"});
        if (!j.contains("request_id") || !j["request_id"].is_number_integer())
          throw std::invalid_argument("INVALID_REQUEST");
        const auto target = j["request_id"];
        if (action_ && action_->id == target)
          FinishAction("CANCELLED");
        if (encoding_ && !encoding_->cancelled &&
            encoding_->capture.id == target) {
          encoding_->cancelled = true;
          Error(target, "CANCELLED");
        }
        for (auto it = captures_.begin(); it != captures_.end();) {
          if (it->id == target) {
            Error(target, "CANCELLED");
            it = captures_.erase(it);
          } else
            ++it;
        }
      } else if (method == "disconnect" || method == "exit") {
        Fields(j, {"id", "method"});
        Stop("CANCELLED");
        session_.Disconnect();
        frame_.reset();
      } else if (method == "video_open") {
        Fields(j, {"id", "method", "ticket", "view", "view_generation",
                    "session", "origin", "apps"});
        if (!video_) throw std::invalid_argument("VIDEO_CHANNEL_UNAVAILABLE");
        if (!session_.Connected()) throw std::invalid_argument("NOT_READY");
        PumpVideo();
        video_->Grant({String(j, "ticket", 32), String(j, "view", 32),
                       String(j, "session", 32), String(j, "origin", 256),
                       Sequence(j, "view_generation"), Flag(j, "apps")});
        Reply(id, {{"url", video_->Url()}, {"transport", "loopback-websocket/1"},
                   {"protocol", decode_path_ == "minirtc" ? "nv12/1" : "h264-annexb/1"},
                   {"decoder", decoder_}, {"expires_in_ms", 15000}});
        return false;
      } else if (method == "video_revoke") {
        Fields(j, {"id", "method", "view"});
        if (video_) video_->Revoke(j.contains("view") ? String(j, "view", 32) : "");
      } else if (method == "status")
        Fields(j, {"id", "method"});
      else
        throw std::invalid_argument("UNSUPPORTED_METHOD");
      PumpVideo();
      Reply(id, Status());
      return method == "exit";
    } catch (const std::invalid_argument &e) {
      Error(id, e.what());
    } catch (const json::exception &) {
      Error(id, "INVALID_REQUEST");
    } catch (...) {
      Error(id, "OPERATION_FAILED");
    }
    return false;
  }
  ControllerSession session_;
  Responses output_;
  SessionOptions options_;
  std::string decode_path_ = "browser", decoder_ = "auto";
  int password_fd_, output_fd_ = -1;
  bool hello_ = false, connected_once_ = false, allowed_ = false,
       paused_ = false;
  uint64_t epoch_ = 0, observation_sequence_ = 0;
  std::shared_ptr<const DecodedFrame> frame_;
  std::deque<Observation> observations_;
  std::deque<Capture> captures_;
  std::unique_ptr<Encoding> encoding_;
  std::unique_ptr<TerminalEncoding> terminal_encoding_;
  std::unique_ptr<RunningAction> action_;
  std::deque<Record> records_;
  json preview_;
  int preview_edge_ = 960;
  Clock::time_point preview_time_;
  std::unique_ptr<VideoServer> video_;
  uint64_t video_generation_ = 0, last_video_frame_ = 0;
  bool video_connected_ = false, video_enabled_ = false;
  bool cursor_visible_ = false;
  float cursor_x_ = 0, cursor_y_ = 0;
  json video_cursor_;
};
} // namespace
int Agent(int output_fd, SessionOptions options, int password_fd,
          bool video_websocket) {
  Automation automation(output_fd, std::move(options), password_fd, video_websocket);
  automation.SetOutputFd(output_fd);
  return automation.Run();
}
} // namespace crossdesk::cli
