#ifndef CROSSDESK_GUI_RENDER_H_
#define CROSSDESK_GUI_RENDER_H_

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

#endif // CROSSDESK_GUI_RENDER_H_
