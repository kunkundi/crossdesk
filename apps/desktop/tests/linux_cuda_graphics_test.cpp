#include "platform/linux/gui/cuda_graphics.h"
#include "rd_log.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Exercise the actual startup policy without requiring a particular GPU or
// X server. NVIDIA offload providers deliberately have a zero capability mask.
std::vector<std::string> provider_names;
std::vector<RRProvider> provider_ids;
bool display_available = true, cuda_available = true, randr_available = true;
bool resources_available = true, info_available = true;
int randr_minor = 4, display_opens = 0, display_closes = 0;

constexpr const char* selection_variables[] = {
    "__NV_PRIME_RENDER_OFFLOAD", "__NV_PRIME_RENDER_OFFLOAD_PROVIDER",
    "__GLX_VENDOR_LIBRARY_NAME", "__EGL_VENDOR_LIBRARY_FILENAMES",
    "__EGL_VENDOR_LIBRARY_DIRS", "DRI_PRIME", "LIBGL_ALWAYS_SOFTWARE",
    "MESA_LOADER_DRIVER_OVERRIDE"};

void Reset(std::vector<std::string> names) {
  for (const auto* name : selection_variables) unsetenv(name);
  setenv("DISPLAY", ":test", 1);
  provider_names = std::move(names);
  provider_ids.resize(provider_names.size());
  for (size_t i = 0; i < provider_ids.size(); ++i) provider_ids[i] = i;
  display_available = cuda_available = randr_available = true;
  resources_available = info_available = true;
  randr_minor = 4;
  display_opens = display_closes = 0;
}

bool CheckSelection(bool offload, const char* scenario) {
  crossdesk::ConfigureLinuxCudaGraphics();
  const char* prime = std::getenv("__NV_PRIME_RENDER_OFFLOAD");
  const char* vendor = std::getenv("__GLX_VENDOR_LIBRARY_NAME");
  const bool matches = offload
      ? prime && std::string(prime) == "1" && vendor && std::string(vendor) == "nvidia"
      : !prime && !vendor;
  if (!matches || display_opens != display_closes) {
    std::cerr << "Incorrect graphics selection or leaked X display: " << scenario << '\n';
    return false;
  }
  return true;
}

}  // namespace

// Link-time substitutes for the external XRandR/CUDA queries, not for the
// selection logic under test. No real display or CUDA context is initialized.
extern "C" {
Display* XOpenDisplay(const char*) {
  if (!display_available) return nullptr;
  ++display_opens;
  return reinterpret_cast<Display*>(&display_opens);
}
int XCloseDisplay(Display*) { ++display_closes; return 0; }
Window XDefaultRootWindow(Display*) { return 1; }
Status XRRQueryVersion(Display*, int* major, int* minor) {
  *major = 1;
  *minor = randr_minor;
  return randr_available;
}
XRRScreenResources* XRRGetScreenResourcesCurrent(Display*, Window) {
  return resources_available ? new XRRScreenResources{} : nullptr;
}
void XRRFreeScreenResources(XRRScreenResources* value) { delete value; }
XRRProviderResources* XRRGetProviderResources(Display*, Window) {
  if (!resources_available) return nullptr;
  auto* result = new XRRProviderResources{};
  result->nproviders = static_cast<int>(provider_ids.size());
  result->providers = provider_ids.data();
  return result;
}
void XRRFreeProviderResources(XRRProviderResources* value) { delete value; }
XRRProviderInfo* XRRGetProviderInfo(Display*, XRRScreenResources*, RRProvider id) {
  if (!info_available) return nullptr;
  auto* result = new XRRProviderInfo{};
  result->name = provider_names.at(id).data();
  result->nameLen = static_cast<int>(provider_names.at(id).size());
  return result;
}
void XRRFreeProviderInfo(XRRProviderInfo* value) { delete value; }
}

namespace crossdesk {
bool IsCudaDeviceAvailable() { return cuda_available; }
std::shared_ptr<spdlog::logger> get_logger() { return spdlog::default_logger(); }
}  // namespace crossdesk

int main() {
  bool ok = true;
  Reset({"NVIDIA-0", "AMD Radeon Graphics @ pci:0000:05:00.0"});
  ok &= CheckSelection(false, "NVIDIA primary with an AMD display sink");
  Reset({"NVIDIA-0"});
  ok &= CheckSelection(false, "NVIDIA-only desktop");
  Reset({"modesetting", "NVIDIA-G0"});
  ok &= CheckSelection(true, "integrated primary with NVIDIA offload");
  Reset({"modesetting", "NVIDIA-G12"});
  ok &= CheckSelection(true, "numbered offload GPU screen");
  Reset({"modesetting", "NVIDIA-G", "NVIDIA-G0-extra", ""});
  ok &= CheckSelection(false, "no recognized NVIDIA GPU screen");
  Reset({});
  ok &= CheckSelection(false, "no providers");
  Reset({"NVIDIA-G0"});
  cuda_available = false;
  ok &= CheckSelection(false, "CUDA unavailable");
  Reset({"NVIDIA-G0"});
  unsetenv("DISPLAY");
  ok &= CheckSelection(false, "no X display");
  Reset({"NVIDIA-G0"});
  display_available = false;
  ok &= CheckSelection(false, "X display inaccessible");
  Reset({"NVIDIA-G0"});
  randr_available = false;
  ok &= CheckSelection(false, "RandR unavailable");
  Reset({"NVIDIA-G0"});
  randr_minor = 3;
  ok &= CheckSelection(false, "RandR predates provider queries");
  Reset({"NVIDIA-G0"});
  resources_available = false;
  ok &= CheckSelection(false, "RandR resources unavailable");
  Reset({"NVIDIA-G0"});
  info_available = false;
  ok &= CheckSelection(false, "provider info unavailable");

  // Any explicit graphics selection must win, including PRIME=0.
  for (const auto* name : selection_variables) {
    Reset({"modesetting", "NVIDIA-G0"});
    setenv(name, "0", 1);
    crossdesk::ConfigureLinuxCudaGraphics();
    for (const auto* other : selection_variables) {
      const char* value = std::getenv(other);
      const bool preserved = std::string(name) == other
          ? value && std::string(value) == "0" : !value;
      if (!preserved) { std::cerr << "Changed explicit selection: " << name << '\n'; ok = false; }
    }
    if (display_opens) { std::cerr << "Probed explicitly selected graphics\n"; ok = false; }
  }
  return ok ? 0 : 1;
}
