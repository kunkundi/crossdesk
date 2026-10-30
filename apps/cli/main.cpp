#include "terminal_io.h"
#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <terminal_renderer.h>

using namespace crossdesk;
using namespace crossdesk::terminal;
using namespace crossdesk::cli;
using nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace {
std::string Env(const char *name) {
  const auto *p = std::getenv(name);
  return p ? p : "";
}
struct Descriptor {
  int value;
  explicit Descriptor(int v) : value(v) {}
  ~Descriptor() {
    if (value >= 0)
      Close(value);
  }
};
class Output {
public:
  explicit Output(int fd)
      : fd_(fd), nonblocking_(fd, true), last_(Clock::now()) {}
  bool Idle() const { return pending_.empty(); }
  void Put(std::string text) {
    if (!Idle() || text.size() > 2 * 1024 * 1024)
      throw std::runtime_error("output budget exceeded");
    pending_ = std::move(text);
    offset_ = 0;
  }
  bool Pump() {
    auto now = Clock::now();
    credit_ = std::min(
        1048576.0,
        credit_ + std::chrono::duration<double>(now - last_).count() * 1048576);
    last_ = now;
    if (Idle())
      return false;
    auto size =
        std::min({pending_.size() - offset_, size_t(16384), size_t(credit_)});
    if (!size)
      return false;
    auto n = Write(fd_, pending_.data() + offset_, size);
    if (n < 0 && errno != EAGAIN && errno != EINTR)
      throw std::runtime_error("output closed");
    if (n > 0) {
      offset_ += n;
      credit_ -= n;
      bytes += n;
    }
    if (offset_ == pending_.size()) {
      pending_.clear();
      return true;
    }
    return false;
  }
  void Wait(int input_fd, int timeout_ms = 5) const {
    // Wake for writable output instead of adding 5ms after each short PTY
    // write. When rate-limited, wait for input/time to replenish the budget.
    WaitIo(input_fd, !Idle() && credit_ >= 1 ? fd_ : -1, timeout_ms);
  }
  uint64_t bytes = 0;

private:
  int fd_;
  NonBlocking nonblocking_;
  std::string pending_;
  size_t offset_ = 0;
  double credit_ = 1048576;
  Clock::time_point last_;
};
struct Tty {
  int out;
  RawTerminal terminal;
  Tty(int input, int output) : out(output), terminal(input, output) {
    const char start[] = "\033[?1049h\033[?25l\033[?7l\033[?1003h\033[?"
                         "1006h\033[?1004h\033[?2004h\033[2J";
    Write(out, start, sizeof(start) - 1);
  }
  ~Tty() {
    // Cancel any incomplete string/control sequence before restoration.
    std::string end = "\030\033\\" + std::string(kKittyDelete) +
                      "\033[0m\033[?1003l\033[?1006l\033[?1004l\033[?2004l\033["
                      "?7h\033[?25h\033[?1049l";
    Write(out, end.data(), end.size());
  }
};
Viewport Size(int *status_rows = nullptr) {
  auto ws = GetTerminalSize(0);
  const int reserved_rows = std::clamp(int(ws.rows) - 1, 0, 3);
  if (status_rows)
    *status_rows = reserved_rows;
  Viewport v;
  v.columns = std::clamp(int(ws.columns) - 1, 1, 240);
  v.rows = std::clamp(int(ws.rows) - reserved_rows, 1, 100);
  v.row_offset = reserved_rows > 0 ? 1 : 0;
  if (ws.columns && ws.rows) {
    v.pixel_width = int(ws.pixel_width) * v.columns / ws.columns;
    v.pixel_height = int(ws.pixel_height) * v.rows / ws.rows;
  }
  if (ws.pixel_width && ws.pixel_height && ws.columns && ws.rows)
    v.cell_aspect = std::clamp(double(ws.pixel_width) * ws.rows /
                                   (double(ws.pixel_height) * ws.columns),
                               .2, 2.0);
  auto locale = Env("LC_ALL");
  if (locale.empty())
    locale = Env("LC_CTYPE");
  if (locale.empty())
    locale = Env("LANG");
  v.utf8 = locale.find("UTF-8") != std::string::npos ||
           locale.find("utf8") != std::string::npos ||
           locale.find("utf-8") != std::string::npos;
  auto color = Env("COLORTERM");
  v.colors = color == "truecolor" || color == "24bit"            ? 24
             : Env("TERM").find("256color") != std::string::npos ? 256
                                                                 : 8;
#ifdef _WIN32
  v.utf8 = true;
  v.colors = 24;
#endif
  return v;
}
std::string SecretFd(int fd) {
  if (fd < 3 || fd > 1024)
    throw std::runtime_error("password_fd must be a dedicated descriptor >= 3");
  std::string secret;
  auto deadline = Clock::now() + std::chrono::seconds(5);
  while (Clock::now() < deadline && !interrupted) {
    if (!WaitReadable(fd, 20))
      continue;
    char c;
    auto n = Read(fd, &c, 1);
    if (n == 0)
      return secret;
    if (n < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("credential read failed");
    }
    if (c == '\n')
      return secret;
    if (c == '\0' || secret.size() >= 256)
      throw std::runtime_error("invalid credentials");
    secret += c;
  }
  throw std::runtime_error("credential channel timeout");
}
int Integer(const json &j, const char *key, int lo, int hi, int fallback) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_number_integer())
    throw std::invalid_argument(std::string(key) + " must be an integer");
  auto n = j[key].get<int64_t>();
  if (n < lo || n > hi)
    throw std::invalid_argument(std::string(key) + " out of range");
  return int(n);
}
bool Boolean(const json &j, const char *key, bool fallback) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_boolean())
    throw std::invalid_argument(std::string(key) + " must be boolean");
  return j[key];
}
void Quality(SessionOptions &options, const std::string &name) {
  if (name == "low") {
    options.quality = 0;
    options.preview_width = 640;
    options.preview_height = 360;
    options.max_bitrate_bps = 1000000;
  } else if (name == "balanced") {
    options.quality = 1;
    options.preview_width = 1280;
    options.preview_height = 720;
    options.max_bitrate_bps = 2500000;
  } else if (name == "high") {
    options.quality = 2;
    options.preview_width = 1920;
    options.preview_height = 1080;
    options.max_bitrate_bps = 6000000;
  } else if (name == "ultra") {
    options.quality = 2;
    options.preview_width = 3840;
    options.preview_height = 2160;
    options.max_bitrate_bps = 12000000;
  } else
    throw std::invalid_argument("quality must be low, balanced, high or ultra");
}
AnsiDetail Detail(const std::string &name) {
  if (name == "half")
    return AnsiDetail::Half;
  if (name == "quadrant")
    return AnsiDetail::Quadrant;
  throw std::invalid_argument("ansi-detail must be half or quadrant");
}
std::string WaitingScreen(const Viewport &v, const std::string &title,
                          const std::string &message) {
  const std::string lines[] = {title, message, "Ctrl+] then q to quit"};
  const int count = std::min(v.rows, 3);
  const int top = v.row_offset + (v.rows - count) / 2 + 1;
  std::string out;
  for (int i = 0; i < count; ++i) {
    const auto text = lines[i].substr(0, v.columns);
    const auto column = (v.columns - text.size()) / 2 + 1;
    out += "\033[" + std::to_string(top + i) + ";1H\033[0m\033[K\033[" +
           std::to_string(column) + "G" + text;
  }
  return out;
}
int View(SessionOptions options, const std::string &device,
         const std::string &renderer, bool control, int fd, AnsiDetail detail,
         double zoom, bool native) {
  if (!IsTty(0) || !IsTty(fd) || Env("TERM") == "dumb")
    throw std::runtime_error("connecting needs an interactive ANSI terminal; use "
                             "bridge --stdio for a host");
  auto password = Password();
  if (interrupted)
    return 130;
  Tty tty(0, fd);
  Output output(fd);
  bool kitty = false;
  if (renderer != "ansi" && Env("TMUX").empty() && Env("STY").empty()) {
    output.Put(std::string(kKittyQuery) + "\033[c");
    std::string reply;
    auto until = Clock::now() + std::chrono::milliseconds(250);
    while (Clock::now() < until && !interrupted) {
      output.Pump();
      WaitReadable(0, 10);
      char b[1024];
      auto n = Read(0, b, sizeof(b));
      if (n > 0 && reply.size() < 8192)
        reply.append(b, n);
    }
    kitty = KittyReply(reply);
  }
  if (renderer == "kitty" && !kitty)
    throw std::runtime_error(
        "Kitty probe failed or multiplexer active; use --renderer ansi");
  ControllerSession s;
  Input input;
  input.control = control;
  input.zoom = zoom;
  input.native = native;
  const auto started = Clock::now();
  s.Connect(options, device, std::move(password));
  s.SetControl(control);
  std::optional<Grid> displayed, pending;
  bool pending_frame = false;
  int last_status_rows = 3;
  Viewport last_view = Size(&last_status_rows);
  std::optional<DecodedFrame> last_frame;
  uint64_t view_revision = input.view_revision;
  bool redraw = true;
  uint64_t submitted = 0;
  std::optional<Clock::time_point> connected_at, first_frame_at,
      first_output_at;
  bool waiting_visible = false;
  double queue_ms = 0;
  auto next = Clock::now(), status_tick = next;
  const auto pump_output = [&] {
    if (output.Pump() && pending_frame) {
      displayed = std::move(pending);
      pending.reset();
      pending_frame = false;
      ++submitted;
      if (!first_output_at) {
        first_output_at = Clock::now();
        status_tick = *first_output_at;
      }
    }
  };
  while (!interrupted && !input.exit) {
    s.Tick();
    if (s.Connected() && !connected_at) {
      connected_at = Clock::now();
      status_tick = *connected_at;
    }
    char bytes[8192];
    auto n = Read(0, bytes, sizeof(bytes));
    const bool previous_command = input.command_mode;
    const bool previous_control = input.control;
    const auto previous_error = input.error;
    input.Feed(n > 0 ? std::string(bytes, n) : "", s,
               displayed ? &*displayed : nullptr);
    if (previous_command != input.command_mode ||
        previous_control != input.control || previous_error != input.error)
      status_tick = Clock::now();
    pump_output();
    if (s.Finished())
      break;
    auto now = Clock::now();
    int status_rows;
    auto v = Size(&status_rows);
    v.detail = detail;
    v.native = input.native;
    v.pixel_mode = kitty;
    v.zoom = input.zoom;
    v.center_x = input.center_x;
    v.center_y = input.center_y;
    if (input.view_revision != view_revision) {
      view_revision = input.view_revision;
      redraw = true;
    }
    if (output.Idle() &&
        (v.columns != last_view.columns || v.rows != last_view.rows ||
         v.pixel_width != last_view.pixel_width ||
         v.pixel_height != last_view.pixel_height ||
         status_rows != last_status_rows)) {
      output.Put("\033[2J");
      displayed.reset();
      last_view = v;
      last_status_rows = status_rows;
      redraw = true;
      status_tick = now;
    }
    if (output.Idle() && now >= status_tick) {
      auto st = s.Status();
      const auto last_state = st["state"].get<std::string>();
      std::string progress =
          first_output_at               ? "Displaying"
          : s.Connected()               ? "Connected, waiting for first frame"
          : last_state == "registering" ? "Connecting to server"
                                        : "Connecting to device";
      const auto elapsed =
          std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                             now - connected_at.value_or(started))
                             .count()) +
          "s";
      if (!first_output_at)
        progress += " (" + elapsed + ")";
      StatusBar bar;
      bar.progress = progress;
      bar.compact_progress = first_output_at ? "LIVE"
                             : s.Connected() ? "WAIT " + elapsed
                                             : "CONNECT " + elapsed;
      bar.error = input.error;
      bar.control = input.control;
      bar.command_mode = input.command_mode;
      bar.view = std::string(kitty                            ? "KITTY"
                             : detail == AnsiDetail::Quadrant ? "QUAD"
                                                              : "HALF") +
                 (input.native ? " | Detail x" : " | Fit x") +
                 std::to_string(int(input.zoom)) +
                 (last_frame ? " | " + std::to_string(last_frame->width) + "x" +
                                   std::to_string(last_frame->height)
                             : "");
      const auto bps = st["network"]["video_bps"].get<uint64_t>();
      const auto traversal = st["network"]["traversal"].get<int>();
      bar.network =
          std::string(traversal == P2P     ? "P2P  "
                      : traversal == Relay ? "RELAY  "
                                           : "Video  ") +
          (bps < 1000      ? std::to_string(bps) + " bps"
           : bps < 1000000 ? std::to_string(bps / 1000) + " kbps"
                           : std::to_string(bps / 1000000) + "." +
                                 std::to_string(bps / 100000 % 10) + " Mbps");
      std::string screen;
      if (!last_frame && !first_output_at) {
        screen = WaitingScreen(
            v, progress,
            s.Connected() ? "The connection is established; waiting for video."
                          : "Establishing a secure connection...");
        waiting_visible = true;
      }
      const auto lines = StatusLines(bar, v.columns);
      for (int row = 0; row < status_rows; ++row)
        screen += "\033[" +
                  std::to_string(row == 0 ? 1 : v.row_offset + v.rows + row) +
                  ";1H\033[0m\033[K" + (row == 0 ? "\033[7m" : "") +
                  lines[row] + "\033[0m";
      output.Put(std::move(screen));
      status_tick = now + std::chrono::seconds(1);
    }
    if (output.Idle() && now >= next) {
      next = now + std::chrono::milliseconds(1000 / options.fps);
      bool fresh = false;
      if (auto frame = s.TakeFrame(last_frame ? &*last_frame : nullptr);
          frame && frame->generation == s.Generation()) {
        last_frame = std::move(frame);
        fresh = true;
        if (!first_frame_at)
          first_frame_at = last_frame->queued;
      }
      if (last_frame && last_frame->generation != s.Generation())
        last_frame.reset();
      if (last_frame && (fresh || redraw)) {
        if (fresh)
          queue_ms = std::chrono::duration<double, std::milli>(
                         now - last_frame->queued)
                         .count();
        pending = kitty ? Layout(*last_frame, v) : Convert(*last_frame, v);
        // Clear waiting text and Kitty margins without erasing the status bars.
        std::string clear;
        if (waiting_visible)
          for (int row = 0; row < v.rows; ++row)
            clear += "\033[" + std::to_string(v.row_offset + row + 1) +
                     ";1H\033[0m\033[2K";
        output.Put(clear + (kitty ? Kitty(*last_frame, v)
                                 : Difference(*pending, displayed)));
        waiting_visible = false;
        redraw = false;
        pending_frame = true;
        if (output.Idle()) {
          displayed = std::move(pending);
          pending.reset();
          pending_frame = false;
        }
      }
    }
    output.Wait(0);
  }
  auto final = s.Status();
  s.Disconnect();
  auto until = Clock::now() + std::chrono::seconds(2);
  while (!output.Idle() && Clock::now() < until) {
    pump_output();
    output.Wait(-1);
  }
  // These diagnostics go to the private file, never through the active screen.
  const auto elapsed_ms =
      [&](const std::optional<Clock::time_point> &time) -> json {
    return time
               ? json(std::chrono::duration<double, std::milli>(*time - started)
                          .count())
               : json(nullptr);
  };
  std::cerr
      << json{{"session", final},
              {"startup_ms",
               {{"connected", elapsed_ms(connected_at)},
                {"first_frame", elapsed_ms(first_frame_at)},
                {"first_output", elapsed_ms(first_output_at)}}},
              {"mouse_reports", input.mouse_reports},
              {"submitted", submitted},
              {"output_bytes", output.bytes},
              {"last_queue_ms", queue_ms},
              {"elapsed_seconds",
               std::chrono::duration<double>(Clock::now() - started).count()},
              {"submitted_fps",
               submitted / std::max(.001, std::chrono::duration<double>(
                                              Clock::now() - started)
                                              .count())}}
             .dump()
      << "\n";
  if (final["state"] == "failed")
    throw std::runtime_error(final["error"].get<std::string>());
  return interrupted ? 130 : 0;
}
int Bridge(int fd, SessionOptions options, int password_fd) {
  Output output(fd);
  ControllerSession s;
  Input input;
  auto status = [&] {
    auto value = s.Status();
    value["command_mode"] = input.command_mode;
    value["resume_control"] = input.control;
    value["exit_requested"] = input.exit;
    return value;
  };
  Viewport viewport;
  viewport.detail = AnsiDetail::Quadrant;
  NonBlocking input_mode(0);
  std::deque<json> replies;
  std::string buffer;
  bool hello = false, quit = false, eof = false, writing_frame = false;
  uint64_t submitted_frames = 0;
  const auto started = Clock::now();
  uint64_t awaiting = 0;
  std::optional<Grid> current, pending_grid;
  std::optional<DecodedFrame> last_frame;
  uint64_t frame_sequence = 0, view_revision = 0;
  bool redraw = true;
  auto next = Clock::now(), status_tick = next;
  auto respond = [&](json j) {
    if (replies.size() >= 64)
      throw std::runtime_error("bridge response queue exceeded");
    replies.push_back(std::move(j));
  };
  auto quit_deadline = Clock::time_point::max();
  while (!interrupted) {
    s.Tick();
    input.Feed("", s, current ? &*current : nullptr);
    if (output.Pump() && writing_frame) {
      ++submitted_frames;
      writing_frame = false;
    }
    if (!quit) {
      char bytes[8192];
      auto n = Read(0, bytes, sizeof(bytes));
      if (n > 0) {
        buffer.append(bytes, n);
        if (buffer.size() > 65536) {
          respond(
              {{"id", nullptr}, {"ok", false}, {"error", "message_too_large"}});
          quit = true;
        }
      } else if (n == 0) {
        eof = true;
      } else if (errno != EAGAIN && errno != EINTR) {
        quit = true;
      }
      for (int count = 0; !quit && count < 16; ++count) {
        auto end = buffer.find('\n');
        if (end == std::string::npos)
          break;
        auto raw = buffer.substr(0, end);
        buffer.erase(0, end + 1);
        auto j = ParseBoundedJson(raw);
        json id = nullptr;
        try {
          if (!j.is_object())
            throw std::invalid_argument("invalid_json");
          if (!j.contains("id") || !j["id"].is_number_integer())
            throw std::invalid_argument("integer_request_id_required");
          id = j["id"];
          std::string method = j.at("method").get<std::string>();
          if (method == "hello") {
            if (Integer(j, "version", 1, 1, 0) != 1)
              throw std::invalid_argument("unsupported_version");
            hello = true;
          } else if (!hello)
            throw std::invalid_argument("hello_required");
          else if (method == "viewport") {
            auto next_view = viewport;
            auto next_options = options;
            next_view.columns = Integer(j, "columns", 1, 240, 80);
            next_view.rows = Integer(j, "rows", 1, 100, 23);
            next_view.colors = Integer(j, "colors", 8, 256, 24);
            if (next_view.colors != 8 && next_view.colors != 24 &&
                next_view.colors != 256)
              throw std::invalid_argument("unsupported_colors");
            next_view.utf8 = Boolean(j, "utf8", true);
            if (j.contains("detail"))
              next_view.detail = Detail(j.at("detail").get<std::string>());
            next_options.fps = Integer(j, "fps", 1, 30, 8);
            if (j.contains("quality"))
              Quality(next_options, j.at("quality").get<std::string>());
            int next_zoom = Integer(j, "zoom", 1, 32, int(input.zoom));
            bool next_native = Boolean(j, "native", input.native);
            s.SetPreview(next_options.fps, next_options.preview_width,
                         next_options.preview_height, next_options.quality,
                         next_options.max_bitrate_bps);
            viewport = next_view;
            options = next_options;
            input.zoom = next_zoom;
            input.native = next_native;
            redraw = true;
            current.reset();
            awaiting = 0;
          } else if (method == "connect") {
            if (s.Connected() ||
                (!s.Finished() && s.Status()["state"] != "idle"))
              throw std::invalid_argument("session_active");
            options.display = Integer(j, "display", 0, 63, 0);
            auto password = SecretFd(password_fd);
            s.Connect(options, j.at("device").get<std::string>(),
                      std::move(password));
            input.control = false;
            input.command_mode = false;
            input.center_x = input.center_y = .5;
            last_frame.reset();
            redraw = true;
            current.reset();
            awaiting = 0;
          } else if (method == "control") {
            input.control = Boolean(j, "enabled", false);
            s.SetControl(input.control && !input.command_mode);
          } else if (method == "input") {
            auto text = j.at("data").get<std::string>();
            if (text.size() > 16384)
              throw std::invalid_argument("input_too_large");
            input.Feed(text, s, current ? &*current : nullptr);
            if (input.exit)
              quit = true;
            if (!input.error.empty())
              throw std::invalid_argument(input.error);
          } else if (method == "ack") {
            if (!j.at("frame_id").is_number_unsigned() ||
                !j.at("generation").is_number_unsigned())
              throw std::invalid_argument("invalid_frame_ack");
            if (j["generation"] == s.Generation() &&
                j["frame_id"] == awaiting) {
              current = std::move(pending_grid);
              pending_grid.reset();
              awaiting = 0;
            }
          } else if (method == "display") {
            if (!s.SelectDisplay(Integer(j, "index", 0, 63, 0)))
              throw std::invalid_argument("display_unavailable");
            current.reset();
            awaiting = 0;
          } else if (method == "disconnect") {
            s.Disconnect();
            input.control = false;
            input.command_mode = false;
            last_frame.reset();
            current.reset();
            awaiting = 0;
          } else if (method == "exit") {
            quit = true;
          } else if (method != "status")
            throw std::invalid_argument("unsupported_method");
          respond(
              {{"id", id}, {"ok", true}, {"version", 1}, {"status", status()}});
        } catch (const std::exception &) {
          respond({{"id", id},
                   {"ok", false},
                   {"error", "invalid_request_or_unavailable_operation"},
                   {"status", status()}});
        }
      }
    }
    if (eof && buffer.find('\n') == std::string::npos) {
      if (!buffer.empty()) {
        respond(
            {{"id", nullptr}, {"ok", false}, {"error", "incomplete_message"}});
        buffer.clear();
      }
      quit = true;
    }
    auto now = Clock::now();
    if (quit && quit_deadline == Clock::time_point::max()) {
      s.Disconnect();
      quit_deadline = now + std::chrono::seconds(2);
    }
    if (output.Idle()) {
      if (!replies.empty()) {
        output.Put(replies.front().dump() + "\n");
        replies.pop_front();
      } else if (quit)
        break;
      else if (hello && now >= status_tick) {
        output.Put(json{{"event", "state"},
                        {"status", status()},
                        {"output_bytes", output.bytes},
                        {"submitted_frames", submitted_frames},
                        {"elapsed_seconds",
                         std::chrono::duration<double>(now - started).count()}}
                       .dump() +
                   "\n");
        status_tick = now + std::chrono::seconds(1);
      } else if (hello && !awaiting && now >= next) {
        next = now + std::chrono::milliseconds(1000 / options.fps);
        bool fresh = false;
        if (auto f = s.TakeFrame(last_frame ? &*last_frame : nullptr);
            f && f->generation == s.Generation()) {
          last_frame = std::move(f);
          fresh = true;
        }
        if (last_frame && last_frame->generation != s.Generation())
          last_frame.reset();
        if (input.view_revision != view_revision) {
          view_revision = input.view_revision;
          redraw = true;
        }
        if (last_frame && (fresh || redraw)) {
          viewport.zoom = input.zoom;
          viewport.native = input.native;
          viewport.center_x = input.center_x;
          viewport.center_y = input.center_y;
          pending_grid = Convert(*last_frame, viewport);
          redraw = false;
          writing_frame = true;
          awaiting = ++frame_sequence;
          output.Put(
              json{{"event", "frame"},
                   {"frame_id", awaiting},
                   {"decoded_frame_id", last_frame->frame_id},
                   {"generation", last_frame->generation},
                   {"columns", viewport.columns},
                   {"rows", viewport.rows},
                   {"source_width", last_frame->width},
                   {"source_height", last_frame->height},
                   {"zoom", viewport.zoom},
                   {"native", viewport.native},
                   {"source_rect",
                    {pending_grid->source.x, pending_grid->source.y,
                     pending_grid->source.width, pending_grid->source.height}},
                   {"detail", viewport.detail == AnsiDetail::Quadrant
                                  ? "quadrant"
                                  : "half"},
                   {"lines", Lines(*pending_grid)},
                   {"queue_ms", fresh
                                    ? std::chrono::duration<double, std::milli>(
                                          now - last_frame->queued)
                                          .count()
                                    : 0}}
                  .dump() +
              "\n");
        }
      }
    }
    if (now >= quit_deadline)
      break;
    output.Wait(eof ? -1 : 0);
  }
  s.Disconnect();
  return interrupted ? 130 : 0;
}
const char *help = R"(crossdesk-cli <9-digit-device-id> [--renderer auto|ansi|kitty] [--fps 1..30]
               [--display 0..63] [--control] [--server host] [--port 9099]
               [--quality low|balanced|high|ultra] [--ansi-detail quadrant|half] [--zoom 1..32|native]
crossdesk-cli view <9-digit-device-id> [options] (compatibility alias)
crossdesk-cli doctor [--json]
crossdesk-cli bridge --stdio [--password-fd 3] [--server host] [--port 9099]

Connect by device ID: read-only and silent by default; hidden password prompt on the local terminal.
Read-only: c enables control; q or Ctrl+C quits; ? shows local commands.
Ctrl+] enters persistent local mode: c selects control on return; q quits; Esc/Ctrl+C returns.
View keys: click/d native detail, wheel/+/- zoom, arrows/ijkl pan (i up, j left, k down, l right), f fit.
Local mode stays active after commands, paste or focus loss; remote input remains paused.
Ctrl+T and Ctrl+C pass through in control mode; another Ctrl+] leaves local mode and forwards Ctrl+] if control is enabled.
Default ANSI detail: quadrant. Default quality: high (1920x1080); ultra requests up to 3840x2160.
Display index is zero-based. ANSI cannot reproduce full desktop text at fit scale.
--fps controls local output; negotiated previews use supported 1/8/15 FPS.
bridge: private NDJSON v1, credentials on a separate inherited descriptor.
No IP direct connection, audio playback, clipboard or file transfer support.
)";
} // namespace
int main(int argc, char **argv) {
  try {
    std::setlocale(LC_ALL, "");
    InitializeIo();
    if (argc < 2 || std::string(argv[1]) == "--help") {
      std::cout << help;
      return 0;
    }
    std::string command = argv[1];
    if (command == "doctor") {
      if (argc > 3 || (argc == 3 && std::string(argv[2]) != "--json"))
        throw std::invalid_argument("doctor accepts only --json");
      auto v = Size();
      json d = {{"version", 1},
                {"platform", Platform()},
                {"stdin_tty", IsTty(0)},
                {"stdout_tty", IsTty(1)},
                {"term", SafeText(Env("TERM"))},
                {"terminal_program", SafeText(Env("TERM_PROGRAM"))},
                {"input_shortcuts", "local_commands_v2"},
                {"transport_diagnostics", true},
                {"utf8", v.utf8},
                {"colors", v.colors},
                {"multiplexer", !Env("TMUX").empty() || !Env("STY").empty()},
                {"software_decode", true},
                {"kitty", "probe_on_view"},
                {"audio_playback", false},
                {"bridge_version", 1}};
      std::cout << d.dump(argc == 3 ? -1 : 2) << "\n";
      return 0;
    }
    int i = 2;
    if (command != "view" && command != "bridge") {
      if (command.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument(
            "expected a 9-digit device ID or a command; use --help");
      command = "view";
      i = 1;
    }
    SessionOptions options;
    std::string renderer = "auto", device;
    AnsiDetail detail = AnsiDetail::Quadrant;
    double zoom = 1;
    bool control = false, stdio = false, native = false;
    int secret_fd = 3;
    if (command == "view") {
      if (i >= argc)
        throw std::invalid_argument("device ID required");
      device = argv[i++];
      if (device.size() != 9 ||
          device.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("device ID must contain 9 digits");
    }
    auto number = [](const std::string &s, int lo, int hi) {
      size_t used = 0;
      int n = std::stoi(s, &used);
      if (used != s.size() || n < lo || n > hi)
        throw std::invalid_argument("numeric option out of range");
      return n;
    };
    for (; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--control" && command == "view")
        control = true;
      else if (a == "--stdio" && command == "bridge")
        stdio = true;
      else {
        if (i + 1 >= argc)
          throw std::invalid_argument("option value missing");
        std::string value = argv[++i];
        if (a == "--server")
          options.host = value;
        else if (a == "--port")
          options.port = number(value, 1, 65535);
        else if (a == "--fps" && command == "view")
          options.fps = number(value, 1, 30);
        else if (a == "--display" && command == "view")
          options.display = number(value, 0, 63);
        else if (a == "--renderer" && command == "view")
          renderer = value;
        else if (a == "--quality")
          Quality(options, value);
        else if (a == "--ansi-detail" && command == "view")
          detail = Detail(value);
        else if (a == "--zoom" && command == "view") {
          native = value == "native";
          zoom = native ? 1 : number(value, 1, 32);
        } else if (a == "--password-fd" && command == "bridge")
          secret_fd = number(value, 3, 1024);
        else
          throw std::invalid_argument("unsupported option");
      }
    }
    if (renderer != "auto" && renderer != "ansi" && renderer != "kitty")
      throw std::invalid_argument("unknown renderer");
    if (command == "bridge" && !stdio)
      throw std::invalid_argument("bridge requires --stdio");
    Descriptor output(Duplicate(1));
    if (output.value < 0)
      throw std::runtime_error("cannot preserve output channel");
    // All library stdout writes go to diagnostics. Only Output uses the saved
    // fd.
    options.log_directory = Logs();
    if (command == "view") {
      Descriptor logs(OpenLog(options.log_directory + "/client.log"));
      if (logs.value < 0 || Redirect(logs.value, 2) < 0)
        throw std::runtime_error("cannot open diagnostic log");
    }
    if (Redirect(2, 1) < 0)
      throw std::runtime_error("cannot isolate protocol output");
    std::cerr << json{{"event", "startup"},
                      {"executable", SafeText(argv[0], 4096)},
                      {"terminal_program", SafeText(Env("TERM_PROGRAM"))},
                      {"input_shortcuts", "local_commands_v2"},
                      {"log_directory", options.log_directory}}
                     .dump()
              << "\n";
    try {
      return command == "view" ? View(options, device, renderer, control,
                                      output.value, detail, zoom, native)
                               : Bridge(output.value, options, secret_fd);
    } catch (const std::exception &e) {
      if (command == "view") {
        std::string msg = "crossdesk-cli: " + SafeText(e.what()) + "\r\n";
        if (std::string(e.what()) == "connection_lost")
          msg += "The remote connection was lost after connecting.\r\n";
        msg += "Diagnostics: " + SafeText(options.log_directory) + "\r\n";
        Write(output.value, msg.data(), msg.size());
      }
      throw;
    }
  } catch (const std::exception &e) {
    std::cerr << "crossdesk-cli: " << SafeText(e.what()) << "\n";
    return 1;
  }
}
