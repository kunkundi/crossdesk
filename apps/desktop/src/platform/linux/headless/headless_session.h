/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-25
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _HEADLESS_SESSION_H_
#define _HEADLESS_SESSION_H_

#include <functional>

namespace crossdesk {

// Must run before logging, SDL, Slint or any worker threads are initialized.
// Automatically selects the user's existing desktop, or supervises a private Xvfb
// server and the application as children for the duration of a headless session.
int RunWithLinuxDisplay(int argc, char* argv[],
                        const std::function<int()>& run_application,
                        const std::function<bool()>& prepare_headless = {});

}  // namespace crossdesk

#endif