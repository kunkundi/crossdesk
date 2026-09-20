# Build from source

[Back to README](../README_EN.md) · [中文](BUILD.md)

After cloning, run build and packaging commands from the repository root. The [Xmake options](../apps/desktop/xmake/options.lua) and [CI workflow](../.github/workflows/build.yml) define the build configuration. `1.4.3` is an example version for local builds; change both the configuration value and packaging argument together so the app and package versions match.

## Build requirements

- Git, [Xmake](https://xmake.io/guide/quick-start.html), and the platform toolchain listed below.
- The build scripts use Xmake to manage **CMake 3.x (≥ 3.21, < 4.0)**, **Rust 1.92.0**, and other dependencies. The first build downloads tools and source archives. See the [Slint package configuration](../deps/thirdparty/slint/xmake.lua) for the constraints.

## Windows

Install Visual Studio 2022 with the **Desktop development with C++** workload, Windows SDK, and **C++ Clang Compiler for Windows** component, plus Xmake. Windows x64 builds use clang-cl for libyuv to enable SIMD conversion and scaling; the application still uses MSVC. In a developer PowerShell, run:

```powershell
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
xmake f -c -p windows -a x64 -m release --CROSSDESK_VERSION=1.4.3 --USE_CUDA=false -y
xmake b -vy crossdesk
xmake r crossdesk
```

Use `release` for performance measurements; `debug` keeps symbols and disables application compiler optimizations.

Installer packaging also uses NSIS. Copy the repository's [nsProcess.dll](../apps/desktop/scripts/windows/nsProcess.dll) into NSIS's `Plugins/x86-unicode` directory and ensure `makensis` is on `PATH`. After building Release, run:

```powershell
.\apps\desktop\scripts\windows\pkg_x64.ps1 1.4.3
```

See the [Windows packaging script](../apps/desktop/scripts/windows/pkg_x64.ps1) and [CI workflow](../.github/workflows/build.yml) for the full process. Manual deployment must retain the runtime libraries, both service helpers and the `usbmmidd_v2` driver folder (copied into the output directory by xmake after the build); see [Windows Service](../README_EN.md#windows-service).

## macOS

Install Xcode and its command-line tools, plus Xmake, then run:

```bash
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
xmake f -c -m release --target_minver=14.0 --CROSSDESK_VERSION=1.4.3 --USE_CUDA=false -y
xmake b -vy crossdesk
xmake r crossdesk
```

The current CI sets macOS 14.0 as the minimum target for both Intel and Apple Silicon. Check the [workflow](../.github/workflows/build.yml) for its Xcode/SDK versions. Package with the script matching your architecture:

```bash
# Apple Silicon
./apps/desktop/scripts/macos/pkg_arm64.sh 1.4.3
# Intel
./apps/desktop/scripts/macos/pkg_x64.sh 1.4.3
```

The scripts create an `.app` and `.pkg` containing the Slint runtime. Screen Recording and Accessibility grants depend on the app path and signature; use a consistent application bundle when testing host functionality.

## Linux

Linux builds support Ubuntu 20.04 or later on amd64 and arm64. Release packages
use Ubuntu 20.04/glibc 2.31 as their compatibility baseline. The dependency
commands below target Ubuntu 20.04; adapt package names and toolchains for
other distributions or newer versions, or use the build image below to
reproduce CI. Install the base build dependencies first:

```bash
sudo apt-get update
sudo apt-get install -y \
  git curl unzip build-essential gcc-10 g++-10 python3-pip \
  pkg-config binutils dpkg-dev \
  libx11-dev libxext-dev libxrender-dev libxft-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev libxfixes-dev libxv-dev \
  libxtst-dev libxcb-randr0-dev libxcb-xtest0-dev libxcb-xinerama0-dev \
  libxcb-shape0-dev libxcb-xkb-dev libxcb-xfixes0-dev libxcb-shm0-dev \
  libasound2-dev libsndio-dev libpulse-dev \
  libgl1-mesa-dev
```

Ubuntu 20.04's bundled CMake 3.16 is too old. Xmake downloads a CMake 3.x
version within the range above; replacing the system CMake manually is not required.

Ubuntu 20.04's default GCC 9 lacks parts of the C++20 standard library needed
by Slint's C++ API, so the commands above install GCC 10 and the configuration
below selects it explicitly.

Clone the submodules and build a release binary:

```bash
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk

# Run this when the repository has already been cloned
git submodule update --init --recursive

xmake f -c -m release --CROSSDESK_VERSION=1.4.3 --USE_CUDA=false --toolchain=gcc-10 -y
xmake b -vy crossdesk
```

Use `--toolchain=gcc-10` so that both CrossDesk and third-party packages such
as libyuv, which xmake builds during configuration, use GCC 10. Specifying only
`--cc`/`--cxx` may still let dependency builds fall back to the system
`/bin/cc` and `/bin/c++`. Keep `-c` when switching compilers to clear the old
configuration.

The amd64 binary is written to `build/linux/x86_64/release/crossdesk`; the arm64 binary is written to `build/linux/arm64/release/crossdesk`.

For Wayland and DRM, install the D-Bus, DRM, and PipeWire development dependencies.
Ubuntu 20.04 uses the repository's PipeWire header SDK; Ubuntu 22.04 and later
can use the distribution's `libpipewire-0.3-dev` and `libspa-0.2-dev` packages instead:

```bash
sudo apt-get install -y libdbus-1-dev libdrm-dev
# Skip this line if the system PipeWire 0.3 development packages are installed
sudo ./docker/linux-build/install-pipewire-sdk.sh

xmake f -c -m release --USE_WAYLAND=true --USE_DRM=true --USE_CUDA=false \
  --CROSSDESK_VERSION=1.4.3 --toolchain=gcc-10 -y
xmake b -vy crossdesk
```

Build a Debian package after compiling a release binary on the matching architecture:

```bash
# amd64
./apps/desktop/scripts/linux/pkg_amd64.sh 1.4.3

# arm64
./apps/desktop/scripts/linux/pkg_arm64.sh 1.4.3
```

The package scripts install the shared Slint runtime in the private `/usr/lib/crossdesk` directory, so users do not need to install `libslint_cpp.so` separately.
PipeWire is not a mandatory runtime dependency. CrossDesk detects the host's
PipeWire 0.3 runtime dynamically and can still use X11 capture (and DRM when
enabled at build time) when it is unavailable.

## Common build options

```text
--USE_CUDA=true/false: Enable CUDA hardware codec acceleration, disabled by default
--USE_WAYLAND=true/false: Enable Wayland/PipeWire capture on Linux, disabled by default
--USE_DRM=true/false: Enable DRM capture on Linux, disabled by default
--CROSSDESK_PORTABLE=true/false: Build the portable variant, disabled by default
--CROSSDESK_VERSION=xxx: Set the CrossDesk version

# Example
xmake f --CROSSDESK_VERSION=1.4.3 --USE_CUDA=true
```

Run:

```bash
xmake r crossdesk
```

## Development Without CUDA Environment

For **Linux developers who do not have a CUDA environment installed and want to enable hardware codec feature**, a preconfigured [Ubuntu 20.04 compatibility build image](https://hub.docker.com/r/crossdesk/ubuntu20.04) is provided.
This image contains the required build dependencies and produces a single Linux package compatible with the glibc 2.31 baseline.

See the [build image notes](../docker/linux-build/README.md) for image tags and architecture differences. The CUDA example below targets the amd64 image. Inside the container, download the project and run:

```bash
export CUDA_PATH=/usr/local/cuda
export XMAKE_GLOBALDIR=/data

xmake f --root -c -m release --CROSSDESK_VERSION=1.4.3 --USE_CUDA=true --toolchain=gcc-10 -y
xmake b --root -vy crossdesk
```

For **Windows developers without a CUDA environment** installed, run the following command to install the CUDA build environment:

```powershell
xmake require -vy "cuda 12.6.3"
```

After the installation is complete, execute:

```powershell
xmake require --info "cuda 12.6.3"
```

Find the `installdir` field in the output.

From the output above, locate the CUDA installation directory — this is the path pointed to by installdir.
Add this path to your system environment variable CUDA_PATH, or set it in the terminal using:

```powershell
$env:CUDA_PATH = "C:\path\to\cuda_installdir"
```

Then re-run:

```powershell
xmake f --USE_CUDA=true
xmake b -vy crossdesk
```

### Connection issues at runtime

A disconnected status can result from network, server configuration, or certificate trust problems. See the [FAQ](FAQ.md#english) and [self-hosted TLS guide](SELF_HOSTING_EN.md) before changing the installation.

## About Xmake

### Installing Xmake

You can install Xmake using one of the following methods:

Using curl:

```bash
curl -fsSL https://xmake.io/shget.text | bash
```

Using wget:

```bash
wget https://xmake.io/shget.text -O - | bash
```

Using powershell:

```powershell
irm https://xmake.io/psget.text | iex
```

### Build Options

```text
# Select Debug; replace debug with release for a Release build
xmake f -m debug

# Optional build parameters
-r : Rebuild the target
-v : Show detailed build logs
-y : Automatically confirm prompts

# Example
xmake b -vy crossdesk
```

### Run Options

```bash
# Launch the current build in a debugger; this does not select Debug build mode
xmake r -d crossdesk
```

For more information, please refer to the [official Xmake documentation](https://xmake.io/guide/quick-start.html).

## iOS / iPadOS

The native client has a separate Xcode project and requires an arm64 physical device running iOS 16+. See the [iOS development guide](../apps/ios/README.md) for build and media/file-transfer checks. CI exports an unsigned app; device installation requires your own signing.

## Refreshing screenshots

Desktop README images are rendered directly from the current Slint UI. See the [screenshot notes](images/README.md) for reproduction commands.
