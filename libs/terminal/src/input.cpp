#include "terminal_renderer.h"
#include <algorithm>
#include <charconv>
namespace crossdesk::terminal {
void Input::Tap(ControllerSession &s, int key, int modifier) {
  if (modifier)
    s.Key(modifier, true);
  s.Key(key, true);
  s.Key(key, false);
  if (modifier)
    s.Key(modifier, false);
}
bool Input::LocalKey(int key, ControllerSession &s, const Grid *grid) {
  if (key == 29) {
    if (command_mode) {
      command_mode = false;
      s.SetControl(control);
      if (control)
        Tap(s, 0xDD, 17); // VK_OEM_6: right bracket, not ASCII ']'.
    } else {
      command_mode = true;
      // Release held buttons and suspend remote input while choosing a command.
      s.SetControl(false);
    }
    return true;
  }
  if (control && !command_mode)
    return false;
  if (key == '?') {
    command_mode = true;
    s.SetControl(false);
    return true;
  }
  if (command_mode && (key == 27 || key == 3)) {
    command_mode = false;
    s.SetControl(control);
    return true;
  }
  if (key == 'q' || key == 3) {
    exit = true;
    command_mode = false;
    control = false;
  } else if (key == 'c') {
    control = !control;
  } else if (key == '+' || key == '=' || key == '-' || key == 'f' ||
             key == 'd' || key == 'i' || key == 'j' || key == 'k' ||
             key == 'l') {
    Navigate(char(key), grid);
  } else if (!command_mode) {
    return false;
  } else {
    error = "Unknown local command; ? for help";
  }
  s.SetControl(control && !command_mode);
  return true;
}
void Input::Navigate(char key, const Grid *grid) {
  if (key == '+' || key == '=')
    zoom = std::min(32.0, zoom * 2);
  if (key == '-')
    zoom = std::max(1.0, zoom / 2);
  if (key == 'f') {
    native = false;
    zoom = 1;
    center_x = center_y = .5;
  }
  if (key == 'd') {
    native = true;
    zoom = 1;
  }
  // Use the displayed crop as the pan step, including native pixel mode.
  const double dx = grid && grid->source_width
                        ? double(grid->source.width) / grid->source_width
                        : 1 / zoom;
  const double dy = grid && grid->source_height
                        ? double(grid->source.height) / grid->source_height
                        : 1 / zoom;
  if (key == 'i' || key == 'j' || key == 'k' || key == 'l') {
    center_x = std::clamp(center_x, dx / 2, 1 - dx / 2);
    center_y = std::clamp(center_y, dy / 2, 1 - dy / 2);
  }
  if (key == 'j')
    center_x -= .2 * dx;
  if (key == 'l')
    center_x += .2 * dx;
  if (key == 'i')
    center_y -= .2 * dy;
  if (key == 'k')
    center_y += .2 * dy;
  center_x = std::clamp(center_x, 0.0, 1.0);
  center_y = std::clamp(center_y, 0.0, 1.0);
  ++view_revision;
}
void Input::Feed(const std::string &bytes, ControllerSession &s,
                 const Grid *grid) {
  if (exit)
    return;
  if (pending_.size() + bytes.size() > 32768) {
    pending_.clear();
    pasting_ = false;
    s.SetControl(false);
    control = false;
    return;
  }
  if (!bytes.empty()) {
    last_input_ = std::chrono::steady_clock::now();
    error.clear();
  }
  if (bytes.empty() && pending_ == "\033" &&
      std::chrono::steady_clock::now() - last_input_ >
          std::chrono::milliseconds(80)) {
    if (!LocalKey(27, s, grid))
      Tap(s, 27);
    pending_.clear();
    return;
  }
  pending_ += bytes;
  while (!pending_.empty()) {
    if (pasting_) {
      auto end = pending_.find("\033[201~");
      if (end == std::string::npos)
        return; // bounded by 32 KiB; one UTF-8 transaction
      // Drop local-mode paste without leaving the mode or running commands.
      if (control && !command_mode && !s.Text(pending_.substr(0, end)))
        error = "Remote UTF-8 text input unavailable";
      pending_.erase(0, end + 6);
      pasting_ = false;
      continue;
    }
    unsigned char c = pending_[0];
    if (c == 29) {
      LocalKey(c, s, grid);
      pending_.erase(0, 1);
      continue;
    }
    if (c == 27) {
      if (pending_.size() < 2)
        return;
      if (pending_.compare(0, 6, "\033[200~") == 0) {
        pasting_ = true;
        pending_.erase(0, 6);
        continue;
      }
      if (pending_[1] == '_') {
        auto end = pending_.find("\033\\", 2);
        if (end == std::string::npos)
          return;
        pending_.erase(0, end + 2);
        continue;
      }
      if (pending_[1] == '[' || pending_[1] == 'O') {
        size_t end = 2;
        while (end < pending_.size() &&
               !(pending_[end] >= '@' && pending_[end] <= '~'))
          ++end;
        if (end == pending_.size())
          return;
        std::string seq = pending_.substr(0, end + 1);
        pending_.erase(0, end + 1);
        if (seq == "\033[O") {
          control = false;
          s.SetControl(false);
          continue;
        }
        int key = 0;
        if (seq == "\033[A" || seq == "\033OA")
          key = 38;
        else if (seq == "\033[B" || seq == "\033OB")
          key = 40;
        else if (seq == "\033[C" || seq == "\033OC")
          key = 39;
        else if (seq == "\033[D" || seq == "\033OD")
          key = 37;
        else if (seq == "\033[H")
          key = 36;
        else if (seq == "\033[F")
          key = 35;
        else if (seq == "\033[3~")
          key = 46;
        else if (seq == "\033[5~")
          key = 33;
        else if (seq == "\033[6~")
          key = 34;
        else if (seq == "\033OP")
          key = 112;
        else if (seq == "\033OQ")
          key = 113;
        else if (seq == "\033OR")
          key = 114;
        else if (seq == "\033OS")
          key = 115;
        if (key) {
          if ((!control || command_mode) && key >= 37 && key <= 40) {
            LocalKey(key == 37   ? 'j'
                     : key == 38 ? 'i'
                     : key == 39 ? 'l'
                                 : 'k',
                     s, grid);
            continue;
          }
          if (command_mode)
            LocalKey(0, s, grid);
          else
            Tap(s, key);
          continue;
        }
        int b = 0, x = 0, y = 0;
        char action = seq.back();
        bool mouse =
            seq.rfind("\033[<", 0) == 0 && (action == 'M' || action == 'm');
        const char *cur = seq.data() + std::min(size_t(3), seq.size());
        const char *limit = seq.data() + seq.size() - 1;
        for (int *value : {&b, &x, &y}) {
          if (!mouse)
            break;
          auto result = std::from_chars(cur, limit, *value);
          mouse = result.ec == std::errc{} && *value >= 0 && *value <= 10000;
          cur = result.ptr;
          if (value != &y) {
            mouse = mouse && cur < limit && *cur == ';';
            if (mouse)
              ++cur;
          } else
            mouse = mouse && cur == limit;
        }
        if (mouse)
          ++mouse_reports;
        if (grid && mouse && !command_mode) {
          auto pos = MapPointer(*grid, x - 1, y - 1);
          if (!control) {
            if (pos && action == 'M' && ((b & 64) || b == 0)) {
              center_x = pos->first;
              center_y = pos->second;
              if (b & 64)
                Navigate((b & 1) ? '-' : '+', grid);
              else
                Navigate('d', grid);
            }
            continue;
          }
          if (!pos && action != 'm')
            continue;
          if (pos) {
            pointer_x_ = pos->first;
            pointer_y_ = pos->second;
          }
          int flag = 0, wheel = 0;
          if (b & 64) {
            flag = (b & 2) ? 8 : 7;
            wheel = (b & 1) ? -120 : 120;
          } else if (!(b & 32)) {
            int base = b & 3;
            if (base == 3)
              continue;
            flag = (base == 0   ? 1
                    : base == 1 ? 5
                                : 3) +
                   (action == 'm' ? 1 : 0);
          }
          s.Pointer(pointer_x_, pointer_y_, flag, wheel);
        }
        continue;
      }
      // Alt+ASCII letter: a tap with explicit modifier release.
      if (command_mode)
        LocalKey(0, s, grid);
      else if (pending_[1] >= 'a' && pending_[1] <= 'z')
        Tap(s, pending_[1] - 'a' + 'A', 18);
      pending_.erase(0, 2);
      continue;
    }
    if (c < 32 || c == 127) {
      pending_.erase(0, 1);
      if (LocalKey(c, s, grid)) {
        if (exit)
          return;
      } else if (c == 13 || c == 10)
        Tap(s, 13);
      else if (c == 9)
        Tap(s, 9);
      else if (c == 127 || c == 8)
        Tap(s, 8);
      else if (c >= 1 && c <= 26)
        Tap(s, 'A' + c - 1, 17);
      continue;
    }
    size_t n = c < 128              ? 1
               : (c & 0xe0) == 0xc0 ? 2
               : (c & 0xf0) == 0xe0 ? 3
               : (c & 0xf8) == 0xf0 ? 4
                                    : 1;
    if (pending_.size() < n)
      return;
    auto text = pending_.substr(0, n);
    pending_.erase(0, n);
    if (LocalKey(c, s, grid)) {
      if (exit)
        return;
    } else if (control && !s.Text(text))
      error = "Remote UTF-8 text input unavailable";
  }
}
} // namespace crossdesk::terminal
