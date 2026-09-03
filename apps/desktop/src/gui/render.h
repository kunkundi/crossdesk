/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RENDER_H_
#define _RENDER_H_

#include <memory>

namespace crossdesk {

class GuiApplication;

// Stable application-facing facade. The SDL application and feature
// controllers remain private implementation details.
class Render {
public:
  Render();
  ~Render();

  Render(const Render &) = delete;
  Render &operator=(const Render &) = delete;

  int Run();

private:
  std::unique_ptr<GuiApplication> application_;
};

} // namespace crossdesk

#endif