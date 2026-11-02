/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-02
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _AGENT_H_
#define _AGENT_H_

#include <controller_session.h>

namespace crossdesk::cli {
int Agent(int output_fd, SessionOptions options, int password_fd,
          bool video_websocket = false);
}

#endif
