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
#include <spa/param/param.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/buffer/meta.h>
#include <spa/utils/result.h>

#if defined(__has_include)
#if __has_include(<spa/param/buffers.h>)
#include <spa/param/buffers.h>
#endif
#endif

#define CROSSDESK_SPA_PARAM_BUFFERS_BUFFERS 1u
#define CROSSDESK_SPA_PARAM_BUFFERS_BLOCKS 2u
#define CROSSDESK_SPA_PARAM_BUFFERS_SIZE 3u
#define CROSSDESK_SPA_PARAM_BUFFERS_STRIDE 4u

#define CROSSDESK_SPA_PARAM_META_TYPE 1u
#define CROSSDESK_SPA_PARAM_META_SIZE 2u

#else

#define CROSSDESK_WAYLAND_BUILD_ENABLED 0

#endif

#endif
