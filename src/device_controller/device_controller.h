/*
 * @Author: DI JUNKUN
 * @Date: 2023-12-14
 * Copyright (c) 2023 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _DEVICE_CONTROLLER_H_
#define _DEVICE_CONTROLLER_H_

#include <stdio.h>

typedef enum {
  mouse = 0,
  keyboard,
  audio_capture,
  host_infomation
} ControlType;
typedef enum { move = 0, left_down, left_up, right_down, right_up } MouseFlag;
typedef enum { key_down = 0, key_up } KeyFlag;
typedef struct {
  size_t x;
  size_t y;
  MouseFlag flag;
} Mouse;

typedef struct {
  size_t key_value;
  KeyFlag flag;
} Key;

typedef struct {
  char host_name[64];
  size_t host_name_size;
} HostInfo;

typedef struct {
  ControlType type;
  union {
    Mouse m;
    Key k;
    HostInfo i;
    bool a;
  };
} RemoteAction;

// int key_code, bool is_down
typedef void (*OnKeyAction)(int, bool, void*);

class DeviceController {
 public:
  virtual ~DeviceController() {}

 public:
  // virtual int Init(int screen_width, int screen_height);
  // virtual int Destroy();
  // virtual int SendCommand(RemoteAction remote_action);

  // virtual int Hook();
  // virtual int Unhook();
};

#endif