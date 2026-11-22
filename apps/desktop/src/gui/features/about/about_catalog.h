/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ABOUT_CATALOG_H_
#define _ABOUT_CATALOG_H_

#include <string>

#include "crossdesk_ui.h"

namespace crossdesk::about {

void SetInstalledReleaseNotes(const slint::ComponentHandle<ui::MainWindow>& window,
                              const std::string& language);
void Populate(const slint::ComponentHandle<ui::MainWindow>& window);

}  // namespace crossdesk::about

#endif
