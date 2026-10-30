# Build from source

[Back to README](../README_EN.md) · [中文](BUILD.md)

After cloning, run build and packaging commands from the repository root. The [Xmake options](../apps/desktop/xmake/options.lua) and [CI workflow](../.github/workflows/build.yml) define the build configuration. `1.4.3` is an example version for local builds; change both the configuration value and packaging argument together so the app and package versions match.

## Build requirements

The default configuration builds the desktop client. Use `--CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y` for the terminal controller alone, or enable both options to build both clients. See the [CLI guide](../apps/cli/README.md) (Chinese) for dependency preparation, usage and npm packaging.

- Git, [Xmake](https://xmake.io/guide/quick-start.html) (3.1.1 or newer for both desktop and mobile), and the platform toolchain listed below.
- The build scripts use Xmake to manage **CMake 3.x (≥ 3.21, < 4.0)**, **Rust 1.92.0**, and other dependencies. The first build downloads tools and source archives. See the [Slint package configuration](../deps/thirdparty/slint/xmake.lua) for the constraints.

The HarmonyOS Slint desktop port compiles the shared desktop UI and application logic; see its [build and parity validation guide (Chinese)](../apps/harmony/desktop-port/README.md) (API 26). The separate ArkUI phone / tablet project is documented in the [HarmonyOS guide (Chinese)](../apps/harmony/README.md). Use the respective scripts instead of reconfiguring the desktop root project.

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

### Developer ID signing and notarization

By default, packaging uses ad-hoc app signatures and leaves the installer unsigned for local development. For distribution, install both `Developer ID Application` and `Developer ID Installer` certificates and their private keys from the same team in your keychain. Configure their full names and your team ID:

```bash
export CROSSDESK_MACOS_SIGN_IDENTITY='Developer ID Application: Your Organization (TEAMID)'
export CROSSDESK_MACOS_INSTALLER_IDENTITY='Developer ID Installer: Your Organization (TEAMID)'

# One-time setup: enter your Apple ID and app-specific password when prompted.
xcrun notarytool store-credentials crossdesk-notary --team-id TEAMID
export CROSSDESK_MACOS_NOTARY_PROFILE=crossdesk-notary

# Build in release mode first, as above. Use pkg_x64.sh for Intel.
./apps/desktop/scripts/macos/pkg_arm64.sh 1.4.3
```

The scripts sign the Slint runtime, updater and app, enable Hardened Runtime with secure timestamps and required resource entitlements, and sign the `.pkg` using the Installer certificate. When a notary profile is configured, they submit to Apple, staple only an `Accepted` result, and verify Gatekeeper assessment. Without a profile, the result is signed but not notarized. Signing and notarization do not replace user consent for Screen Recording, Accessibility or other protected resources.

To notarize an existing signed installer separately:

```bash
CROSSDESK_MACOS_NOTARY_PROFILE=crossdesk-notary \
  bash apps/desktop/scripts/macos/notarize_pkg.sh crossdesk-macos-arm64-1.4.3.pkg
```

After a successful upload, the submission ID is printed immediately and saved in `.notary-submit.json`. The result and log are saved next to the installer as `.notary.json` and `.notary-log.json`. Failure or timeout stops packaging and preserves the artifact and submission ID. The default wait is 30 minutes; override it with `CROSSDESK_MACOS_NOTARY_TIMEOUT`. After a timeout, resume waiting with `xcrun notarytool wait <submission-id> --keychain-profile crossdesk-notary`; after confirming `Accepted`, run `xcrun stapler staple <package>`, `xcrun stapler validate <package>` and `spctl --assess --type install --verbose=2 <package>`.

If accelerated uploads time out, set `CROSSDESK_MACOS_NOTARY_S3_ACCELERATION=false` to retry using the standard S3 upload endpoint; the default is `true`. A failed upload may still allocate a submission record at Apple, so check `xcrun notarytool history --keychain-profile crossdesk-notary` before retrying.

Set `CROSSDESK_MACOS_KEYCHAIN` to use a custom keychain containing both signing identities and the notary profile. Never commit private keys, P12 files or passwords. See Apple's [Developer ID certificate guide](https://developer.apple.com/help/account/certificates/create-developer-id-certificates/) and [notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

### Signing and notarization in GitHub Actions

The [Build and Release workflow](../.github/workflows/build.yml) automatically signs, notarizes and staples macOS installers for `v*` release tags. Manual runs can enable `sign_macos`; ordinary branch pushes and unchecked manual runs retain ad-hoc signing. Each architecture is processed on its own runner. Signed builds upload their normal artifacts only after notarization and Gatekeeper verification succeed; missing secrets or notarization failures block publication.

In **Keychain Access → My Certificates**, export the `Developer ID Application` and `Developer ID Installer` identities **with their private keys** into two `.p12` files, using the same export password for both. A `.cer` file alone is insufficient. Keep these files outside the repository, for example `~/Downloads/crossdesk-application.p12` and `~/Downloads/crossdesk-installer.p12`.

Add these repository secrets under **Settings → Secrets and variables → Actions → New repository secret**:

| Secret | Value |
| --- | --- |
| `MACOS_APP_CERTIFICATE_P12_BASE64` | Base64 of the Application certificate/private-key `.p12` |
| `MACOS_INSTALLER_CERTIFICATE_P12_BASE64` | Base64 of the Installer certificate/private-key `.p12` |
| `MACOS_P12_PASSWORD` | Shared export password for both `.p12` files |
| `APPLE_ID` | Apple account email with access to the developer team |
| `APPLE_APP_SPECIFIC_PASSWORD` | App-specific password generated for notarization |
| `APPLE_TEAM_ID` | The 10-character team ID of both certificates |

Run these commands separately and paste each clipboard value into its corresponding Base64 secret. Save the first secret before running the second command:

```bash
base64 -i "$HOME/Downloads/crossdesk-application.p12" | pbcopy
base64 -i "$HOME/Downloads/crossdesk-installer.p12" | pbcopy
```

Your local `crossdesk-notary` profile does not synchronize to GitHub runners. The workflow recreates it in a temporary keychain, selects valid identities matching the team ID, allows signing tools to access only those imported keys without interactive prompts, and deletes signing/notarization credentials at the end of the job. No extra keychain password secret or provisioning profile is required for the current Developer ID distribution setup.

After committing and pushing the workflow and supporting scripts, first run it manually with `desktop_version` set and `sign_macos` enabled. Confirm that `Notarize and staple macOS installer` succeeds for both ARM64 and Intel before pushing a release tag.

CI waits up to 60 minutes for notarization. If Apple is still processing, the job fails and blocks publication, while preserving `macos-pending-notarization-*` installer artifacts and `macos-notarization-*` JSON diagnostics for 14 days. Download them and use the saved submission ID as described above to resume checking and staple the package without uploading it again. Pending artifacts are not completed notarized releases. CI signing does not change the installer's existing permission-reset behavior; preserving Accessibility grants requires a separate installer change.

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

Each packaging run uses the same compiled application to create the standard `crossdesk-linux-<arch>-<version>.deb` and `crossdesk-virtual-desktop-xfce-linux-<arch>-<version>.deb`. Both contain the complete application; only the latter requires Xvfb, Xfce and D-Bus session tools. Building and packaging need none of these optional runtime components. To run a complete virtual desktop from source, install them as described in [headless operation](HEADLESS_EN.md). The build image and CI can retain them for testing.

The package scripts install the shared Slint runtime in the private `/usr/lib/crossdesk` directory, so users do not need to install `libslint_cpp.so` separately. The package-managed `package-name` file beside it identifies the update download variant without changing user settings.
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
--CROSSDESK_UPDATE_PUBLIC_KEY=xxx: Optional Ed25519 update verification key, 64 lowercase hex characters; empty by default, disabling in-app update installation

# Example
xmake f --CROSSDESK_VERSION=1.4.3 --USE_CUDA=true
```

Run:

```bash
xmake r crossdesk
```

<a id="update-public-key"></a>

### Update public key (optional)

`CROSSDESK_UPDATE_PUBLIC_KEY` is embedded in the desktop client at build time to verify Ed25519 signatures on update manifests. Local development builds can omit it: the app still builds, runs, and checks for new versions, but only offers manual downloads without in-app update installation.

For in-app update installation, add this option to your platform's `xmake f` configuration command before building. For example, after configuring the platform, run the following with the placeholder replaced by your actual public key:

```sh
xmake f --CROSSDESK_UPDATE_PUBLIC_KEY="<64 lowercase hex characters>"
xmake b crossdesk
```

You can also set the environment variable with the same name before building; the Xmake configuration value takes precedence. The value must encode a 32-byte Ed25519 public key as 64 lowercase hexadecimal characters. A nonempty value with an invalid format fails build configuration. Update manifests must be signed with the matching private key, and the installation must meet the other updater requirements, including a supported installation location. Portable builds always use manual downloads.

GitHub Actions builds read the key from the repository Actions variable `CROSSDESK_UPDATE_PUBLIC_KEY`. The private key is only needed for release signing, not ordinary builds. See the [desktop updater guide](UPDATER.md) (Chinese) for key generation, release configuration, and updater requirements.

## Development Without CUDA Environment

For **Linux developers who do not have a CUDA environment installed and want to enable hardware codec feature**, a preconfigured [Ubuntu 20.04 compatibility build image](https://hub.docker.com/r/crossdesk/ubuntu20.04) is provided.
This image contains the required build dependencies and produces both Linux package variants with the glibc 2.31 baseline.

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

[iOS CI](../.github/workflows/build-ios.yml) can run manually on its own, is called by the main build workflow, and runs for relevant pull requests. It retains the existing toolchain, license/privacy checks, app verification and ZIP packaging. Tag releases continue to collect the unsigned app archive under the same artifact name.

## Refreshing screenshots

Desktop README images are rendered directly from the current Slint UI. See the [screenshot notes](images/README.md) for reproduction commands.

## Android

The native controller is a separate Gradle project in `apps/android`, targeting Android 8.0+ arm64 with SDK 36, NDK r28c, and Xmake 3.1.1 or newer. See the [Android development guide](../apps/android/README.md) for APK builds, installation, and emulator tests.

[Android CI](../.github/workflows/build-android.yml) can run manually on its own and is also called by the main build workflow. It uploads Debug/device-test APKs, an unsigned Release APK and Lint reports, runs native protocol tests, and checks 16 KB alignment and license contents. Tag releases include the unsigned Release APK, which requires your own signing before installation. Device tests must be run separately on an arm64 phone or emulator.
