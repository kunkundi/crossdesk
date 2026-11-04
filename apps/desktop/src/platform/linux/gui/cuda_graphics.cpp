#include "platform/linux/gui/cuda_graphics.h"

#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string_view>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include "rd_log.h"

#include "platform/common/gui/cuda_opengl_interop.h"

namespace crossdesk {
namespace {

#if USE_CUDA && (defined(__x86_64__) || defined(__amd64__))
bool HasNvidiaOffloadProvider() {
  // CUDA availability says nothing about the X server's PRIME topology.
  // In particular, NVIDIA-0 is a normal display provider, not an offload
  // GPU screen. Forcing offload in that configuration can render into a GL
  // buffer successfully while leaving the actual window blank.
  const char* display_name = std::getenv("DISPLAY");
  if (!display_name || display_name[0] == '\0') return false;
  std::unique_ptr<Display, decltype(&XCloseDisplay)> display(
      XOpenDisplay(display_name), XCloseDisplay);
  if (!display) return false;

  int major = 0, minor = 0;
  if (!XRRQueryVersion(display.get(), &major, &minor) || major < 1 ||
      (major == 1 && minor < 4)) return false;

  const auto root = XDefaultRootWindow(display.get());
  std::unique_ptr<XRRScreenResources, decltype(&XRRFreeScreenResources)> screen(
      XRRGetScreenResourcesCurrent(display.get(), root), XRRFreeScreenResources);
  std::unique_ptr<XRRProviderResources, decltype(&XRRFreeProviderResources)>
      providers(XRRGetProviderResources(display.get(), root),
                XRRFreeProviderResources);
  if (!screen || !providers) return false;

  for (int i = 0; i < providers->nproviders; ++i) {
    std::unique_ptr<XRRProviderInfo, decltype(&XRRFreeProviderInfo)> provider(
        XRRGetProviderInfo(display.get(), screen.get(), providers->providers[i]),
        XRRFreeProviderInfo);
    if (!provider || !provider->name || provider->nameLen <= 0) continue;
    const std::string_view name(provider->name, provider->nameLen);
    // NVIDIA documents offload GPU screens as NVIDIA-G0, NVIDIA-G1, etc.
    // Their RandR capability mask can be zero, so SourceOffload is not a
    // reliable test for the proprietary driver's offload support.
    constexpr std::string_view prefix = "NVIDIA-G";
    if (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix &&
        name.find_first_not_of("0123456789", prefix.size()) ==
            std::string_view::npos) return true;
  }
  return false;
}
#endif

}  // namespace

void ConfigureLinuxCudaGraphics() {
#if USE_CUDA && (defined(__x86_64__) || defined(__amd64__))
  // Respect explicit GPU/driver selection, including a request to use Mesa
  // or software rendering. Native frames can still use the CPU fallback.
  for (const char* name : {"__NV_PRIME_RENDER_OFFLOAD",
                           "__NV_PRIME_RENDER_OFFLOAD_PROVIDER",
                           "__GLX_VENDOR_LIBRARY_NAME",
                           "__EGL_VENDOR_LIBRARY_FILENAMES",
                           "__EGL_VENDOR_LIBRARY_DIRS", "DRI_PRIME",
                           "LIBGL_ALWAYS_SOFTWARE",
                           "MESA_LOADER_DRIVER_OVERRIDE"}) {
    const char* value = std::getenv(name);
    if (value && value[0] != '\0') {
      LOG_INFO("Keeping explicit Linux graphics selection: {}={}", name,
               value);
      return;
    }
  }

  if (!HasNvidiaOffloadProvider()) {
    LOG_INFO("Keeping default Linux graphics selection: no NVIDIA PRIME "
             "offload GPU screen");
    return;
  }
  if (!IsCudaDeviceAvailable()) {
    return;
  }

  // Both EGL and GLX need to render on NVIDIA to share CUDA frame buffers.
  // These are process-local, not desktop settings.
  // https://download.nvidia.com/XFree86/Linux-x86_64/595.45.04/README/primerenderoffload.html
  if (setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1) != 0 ||
      setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1) != 0) {
    LOG_WARN("Unable to select NVIDIA PRIME rendering for CUDA video");
    return;
  }
  LOG_INFO("Selected NVIDIA PRIME rendering for CUDA/OpenGL video interop");
#endif
}

}  // namespace crossdesk
