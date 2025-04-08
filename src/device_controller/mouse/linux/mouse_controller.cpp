#include "mouse_controller.h"

#include "log.h"

MouseController::MouseController() {}

MouseController::~MouseController() {
  if (uinput_fd_) {
    ioctl(uinput_fd_, UI_DEV_DESTROY);
    close(uinput_fd_);
  }
}

int MouseController::Init(int screen_width, int screen_height) {
  screen_width_ = screen_width;
  screen_height_ = screen_height;

  uinput_fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinput_fd_ < 0) {
    LOG_ERROR("Cannot open device: /dev/uinput");
    return -1;
  }

  ioctl(uinput_fd_, UI_SET_EVBIT, EV_KEY);
  ioctl(uinput_fd_, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(uinput_fd_, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(uinput_fd_, UI_SET_EVBIT, EV_ABS);
  ioctl(uinput_fd_, UI_SET_ABSBIT, ABS_X);
  ioctl(uinput_fd_, UI_SET_ABSBIT, ABS_Y);
  ioctl(uinput_fd_, UI_SET_EVBIT, EV_REL);

  struct uinput_user_dev uidev;
  memset(&uidev, 0, sizeof(uidev));
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "VirtualMouse");
  uidev.id.bustype = BUS_USB;
  uidev.id.version = 1;
  uidev.id.vendor = 0x1;
  uidev.id.product = 0x1;
  uidev.absmin[ABS_X] = 0;
  uidev.absmax[ABS_X] = screen_width_;
  uidev.absmin[ABS_Y] = 0;
  uidev.absmax[ABS_Y] = screen_height_;

  int res_uidev = write(uinput_fd_, &uidev, sizeof(uidev));
  ioctl(uinput_fd_, UI_DEV_CREATE);
  return 0;
}

int MouseController::Destroy() { return 0; }

int MouseController::SendMouseCommand(RemoteAction remote_action) {
  int mouse_pos_x = remote_action.m.x;
  int mouse_pos_y = remote_action.m.y;

  if (remote_action.type == ControlType::mouse) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

    if (remote_action.m.flag == MouseFlag::left_down) {
      SimulateKeyDown(uinput_fd_, BTN_LEFT);
    } else if (remote_action.m.flag == MouseFlag::left_up) {
      SimulateKeyUp(uinput_fd_, BTN_LEFT);
    } else if (remote_action.m.flag == MouseFlag::right_down) {
      SimulateKeyDown(uinput_fd_, BTN_RIGHT);
    } else if (remote_action.m.flag == MouseFlag::right_up) {
      SimulateKeyUp(uinput_fd_, BTN_RIGHT);
    } else {
      SetMousePosition(uinput_fd_, mouse_pos_x, mouse_pos_y);
    }
  }

  return 0;
}

void MouseController::SimulateKeyDown(int fd, int kval) {
  int res_ev = 0;
  struct input_event event;
  memset(&event, 0, sizeof(event));
  gettimeofday(&event.time, 0);

  event.type = EV_KEY;
  event.value = 1;
  event.code = kval;
  res_ev = write(fd, &event, sizeof(event));

  event.type = EV_SYN;
  event.value = 0;
  event.code = SYN_REPORT;
  res_ev = write(fd, &event, sizeof(event));
}

void MouseController::SimulateKeyUp(int fd, int kval) {
  int res_ev = 0;
  struct input_event event;
  memset(&event, 0, sizeof(event));
  gettimeofday(&event.time, 0);

  event.type = EV_KEY;
  event.value = 0;
  event.code = kval;
  res_ev = write(fd, &event, sizeof(event));

  event.type = EV_SYN;
  event.value = 0;
  event.code = SYN_REPORT;
  res_ev = write(fd, &event, sizeof(event));
}

void MouseController::SetMousePosition(int fd, int x, int y) {
  struct input_event ev[2], ev_sync;
  memset(ev, 0, sizeof(ev));
  memset(&ev_sync, 0, sizeof(ev_sync));

  ev[0].type = EV_ABS;
  ev[0].code = ABS_X;
  ev[0].value = x;
  ev[1].type = EV_ABS;
  ev[1].code = ABS_Y;
  ev[1].value = y;
  int res_w = write(fd, ev, sizeof(ev));

  ev_sync.type = EV_SYN;
  ev_sync.value = 0;
  ev_sync.code = 0;
  int res_ev_sync = write(fd, &ev_sync, sizeof(ev_sync));
}