/*
 * @Author: DI JUNKUN
 * @Date: 2026-03-22
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SCREEN_CAPTURER_WAYLAND_BUILD_H_
#define _SCREEN_CAPTURER_WAYLAND_BUILD_H_

#if defined(CROSSDESK_HAS_WAYLAND_CAPTURER) && CROSSDESK_HAS_WAYLAND_CAPTURER

#define CROSSDESK_WAYLAND_BUILD_ENABLED 1

#include <dbus/dbus.h>
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <pipewire/thread-loop.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/utils/result.h>

#else

#define CROSSDESK_WAYLAND_BUILD_ENABLED 0

#endif

#endif
