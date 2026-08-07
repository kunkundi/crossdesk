# CrossDesk

<a href="https://hellogithub.com/repository/kunkundi/crossdesk" target="_blank"><img src="https://api.hellogithub.com/v1/widgets/recommend.svg?rid=55d41367570345f1838e02fd12be7961&claim_uid=cb0OpZRrBuGVAfL&theme=small" alt="Featured｜HelloGitHub" /></a>

[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-brightgreen.svg)](https://www.crossdesk.cn/)
[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![GitHub last commit](https://img.shields.io/github/last-commit/kunkundi/crossdesk)](https://github.com/kunkundi/crossdesk/commits/web-client)
[![Build Status](https://github.com/kunkundi/crossdesk/actions/workflows/build.yml/badge.svg)](https://github.com/kunkundi/crossdesk/actions)  
[![Docker Pulls](https://img.shields.io/docker/pulls/crossdesk/crossdesk-server)](https://hub.docker.com/r/crossdesk/crossdesk-server/tags)
[![GitHub issues](https://img.shields.io/github/issues/kunkundi/crossdesk.svg)](https://github.com/kunkundi/crossdesk/issues)
[![GitHub stars](https://img.shields.io/github/stars/kunkundi/crossdesk.svg?style=social)](https://github.com/kunkundi/crossdesk/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/kunkundi/crossdesk.svg?style=social)](https://github.com/kunkundi/crossdesk/forks)

[ [中文](README.md) / English ]

---

## Preview

### PC Client

![sup_example](https://github.com/user-attachments/assets/3f17d8f3-7c4a-4b63-bae4-903363628687)

### Web Client

<p align="center">
  <img width="850" height="550" alt="6bddcbed47ffd4b9988a4037c7f4f524" src="https://github.com/user-attachments/assets/e44f73f9-24ac-46a3-a189-b7f8b6669881" />
</p>

---

## Introduction

CrossDesk is a lightweight cross-platform remote desktop software.

CrossDesk is an experimental application of [MiniRTC](https://github.com/kunkundi/minirtc.git), a lightweight cross-platform real-time audio and video transmission library. MiniRTC provides fundamental capabilities including network traversal ([RFC5245](https://datatracker.ietf.org/doc/html/rfc5245)), video software/hardware encoding and decoding (H264/AV1), audio encoding/decoding ([Opus](https://github.com/xiph/opus)), signaling interaction, network congestion control, and transmission encryption ([SRTP](https://tools.ietf.org/html/rfc3711)).

---

## System Requirements

| Platform | Minimum Version |
| --- | --- |
| **Windows** | Windows 10 or later (64-bit) |
| **macOS** | macOS Intel 15.0 or later *(versions between 14.0 and 15.0 can be built manually for compatibility)*<br>macOS Apple Silicon 14.0 or later |
| **Linux** | Ubuntu 20.04 or later |

---

## Usage

Enter the remote desktop ID in the menu bar’s “Remote ID” field and click “→” to initiate a remote connection.

![usage1](https://github.com/user-attachments/assets/3a4bb59f-c84c-44d2-9a20-11790aac510e)

If the remote desktop requires a connection password, you must enter the correct password on your side to successfully establish the connection.

![password](https://github.com/user-attachments/assets/1beadcce-640d-4f5c-8e77-51917b5294d5)

Before connecting, you can customize configuration options in the settings, such as language and video encoding format.

![settings](https://github.com/user-attachments/assets/8bc5468d-7bbb-4e30-95bd-da1f352ac08c)

### Using the Web Client

Visit [CrossDesk Web Client](https://web.crossdesk.cn/).
Enter the **Remote Device ID** and **Password**, then click Connect to access the remote device. As shown, **iOS Safari remotely controlling Windows 11**:

<img width="645" height="300" alt="_cgi-bin_mmwebwx-bin_webwxgetmsgimg__ MsgID=932911462648581698 skey=@crypt_1f5153b1_b550ca7462b5009ce03c991cca2a92a7 mmweb_appid=wx_webfilehelper" src="https://github.com/user-attachments/assets/a5109e6f-752c-4654-9f4e-7e161bddf43e" />

### Windows Service (CrossDesk Service)

CrossDesk provides a local helper service on Windows named **CrossDesk Service**. Its service name is `CrossDeskService`. The service improves remote control in protected Windows states such as the lock screen, sign-in UI, credential UI, and secure desktop. It provides:

- Remote status reporting for lock screen, sign-in, credential, and secure desktop states.
- Remote `Ctrl+Alt+Del` (SAS) delivery.
- Keyboard and mouse input forwarding while the remote Windows device is on the lock screen, sign-in UI, or secure desktop.

The Windows installer bundles `crossdesk_service.exe` and `crossdesk_session_helper.exe`, then registers the service as an on-demand Windows service during installation. When the CrossDesk client starts, it tries to start the installed service automatically. When no CrossDesk client process is running on the machine, the service exits automatically. Uninstalling the client also stops and removes the service.

For manual Windows builds or deployments, make sure `CrossDesk.exe`, `crossdesk_service.exe`, and `crossdesk_session_helper.exe` are placed in the same directory. Open PowerShell with administrator privileges to install or uninstall the service:

```powershell
# install
.\CrossDesk.exe --service-install
# start
.\CrossDesk.exe --service-start
# check status
.\CrossDesk.exe --service-status
.\CrossDesk.exe --service-ping
# stop
.\CrossDesk.exe --service-stop
# uninstall
.\CrossDesk.exe --service-uninstall
```

If the remote Windows service is not installed, not running, or temporarily unavailable, the basic remote desktop connection still works, but remote control on the lock screen, sign-in UI, and secure desktop is limited. The client will show “Remote Windows service unavailable”.

---

## How to build

### Build requirements

- [xmake](https://xmake.io/#/guide/installation)
- [cmake](https://cmake.org/download/) 3.21 or later (installed automatically by xmake when the system version is too old)

### Linux

Linux builds support Ubuntu 20.04 or later on amd64 and arm64. Release packages
use Ubuntu 20.04/glibc 2.31 as their compatibility baseline. Install the base
build dependencies first:

```bash
sudo apt-get update
sudo apt-get install -y \
  git curl unzip build-essential gcc-10 g++-10 python3-pip \
  pkg-config binutils dpkg-dev \
  libx11-dev libxext-dev libxrender-dev libxft-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev libxfixes-dev libxv-dev \
  libxtst-dev libxcb-randr0-dev libxcb-xtest0-dev libxcb-xinerama0-dev \
  libxcb-shape0-dev libxcb-xkb-dev libxcb-xfixes0-dev libxcb-shm0-dev \
  libasound2-dev libsndio-dev libpulse-dev
```

When Ubuntu 20.04's bundled CMake 3.16 is too old, xmake automatically
downloads and uses CMake 3.21 or later; replacing the system CMake manually is
not required.

Ubuntu 20.04's default GCC 9 lacks parts of the C++20 standard library needed
by Slint's C++ API, so the commands above install GCC 10 and the configuration
below selects it explicitly.

Install the optional dependencies when Wayland capture or DRM capture is enabled:

```bash
sudo apt-get install -y libdbus-1-dev libdrm-dev

# Ubuntu 22.04 and later can alternatively use the distribution packages.
# sudo apt-get install -y libpipewire-0.3-dev libspa-0.2-dev
```

Clone the submodules and build a release binary:

```bash
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk

# Run this when the repository has already been cloned
git submodule update --init --recursive

# Ubuntu 20.04 has no PipeWire 0.3 development package. Install the
# header-only SDK used during compilation. Skip this when the distribution's
# PipeWire 0.3 development packages are already installed.
sudo ./docker/linux-build/install-pipewire-sdk.sh

xmake f -c -m release --USE_CUDA=false --toolchain=gcc-10 -y
xmake b -vy crossdesk
```

Use `--toolchain=gcc-10` so that both CrossDesk and third-party packages such
as libyuv, which xmake builds during configuration, use GCC 10. Specifying only
`--cc`/`--cxx` may still let dependency builds fall back to the system
`/bin/cc` and `/bin/c++`. Keep `-c` when switching compilers to clear the old
configuration.

The amd64 binary is written to `build/linux/x86_64/release/crossdesk`; the arm64 binary is written to `build/linux/arm64/release/crossdesk`.

Example configuration with Wayland and DRM capture enabled:

```bash
xmake f -c -m release --USE_WAYLAND=true --USE_DRM=true --USE_CUDA=false \
  --toolchain=gcc-10 -y
xmake b -vy crossdesk
```

Build a Debian package after compiling a release binary on the matching architecture:

```bash
# amd64
./scripts/linux/pkg_amd64.sh 1.0.0

# arm64
./scripts/linux/pkg_arm64.sh 1.0.0
```

The package scripts install the shared Slint runtime in the private `/usr/lib/crossdesk` directory, so users do not need to install `libslint_cpp.so` separately.
PipeWire is not a mandatory runtime dependency. CrossDesk detects the host's
PipeWire 0.3 runtime dynamically and can still use X11 capture (and DRM when
enabled at build time) when it is unavailable.

### Common build options

```text
--USE_CUDA=true/false: Enable CUDA hardware codec acceleration, disabled by default
--USE_WAYLAND=true/false: Enable Wayland/PipeWire capture on Linux, disabled by default
--USE_DRM=true/false: Enable DRM capture on Linux, disabled by default
--CROSSDESK_PORTABLE=true/false: Build the portable variant, disabled by default
--CROSSDESK_VERSION=xxx: Set the CrossDesk version

# Example
xmake f --CROSSDESK_VERSION=1.0.0 --USE_CUDA=true
```

Run:

```bash
xmake r crossdesk
```

### Development Without CUDA Environment

For **Linux developers who do not have a CUDA environment installed and want to enable hardware codec feature**, a preconfigured [Ubuntu 20.04 compatibility build image](https://hub.docker.com/r/crossdesk/ubuntu20.04) is provided.
This image contains the required build dependencies and produces a single Linux package compatible with the glibc 2.31 baseline.

After entering the container, download the project and run:

```bash
export CUDA_PATH=/usr/local/cuda
export XMAKE_GLOBALDIR=/data

xmake f --USE_CUDA=true
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

The output will look like this:

<img width="860" height="226" alt="Image" src="https://github.com/user-attachments/assets/999ac365-581a-4b9a-806e-05eb3e4cf44d" />

From the output above, locate the CUDA installation directory — this is the path pointed to by installdir.
Add this path to your system environment variable CUDA_PATH, or set it in the terminal using:

```powershell
set CUDA_PATH=path_to_cuda_installdir
```

Then re-run:

```powershell
xmake f --USE_CUDA=true
xmake b -vy crossdesk
```

#### Notice

If the client status bar shows **Disconnected** during runtime, please first install the client from the [CrossDesk official website](https://www.crossdesk.cn/) to ensure the required certificate files are available in the environment.

<img width="256" height="120" alt="image" src="https://github.com/user-attachments/assets/1812f7d6-516b-4b4f-8a3d-98bee505cc5a" />

### About Xmake

#### Installing Xmake

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

#### Build Options

```text
# Switch build mode
xmake f -m debug/release

# Optional build parameters
-r : Rebuild the target
-v : Show detailed build logs
-y : Automatically confirm prompts

# Example
xmake b -vy crossdesk
```

#### Run Options

```bash
# Run in debug mode
xmake r -d crossdesk
```

For more information, please refer to the [official Xmake documentation](https://xmake.io/guide/quick-start.html).

---

## Self-Hosted Server

### Server

It is recommended to deploy CrossDesk Server using Docker.

```bash
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=xxx.xxx.xxx.xxx \
  -e INTERNAL_IP=xxx.xxx.xxx.xxx \
  -e CROSSDESK_SERVER_PORT=xxxx \
  -e COTURN_PORT=xxxx \
  -e MIN_PORT=xxxxx \
  -e MAX_PORT=xxxxx \
  -v /var/lib/crossdesk:/var/lib/crossdesk \
  -v /var/log/crossdesk:/var/log/crossdesk \
  crossdesk/crossdesk-server:v1.1.6
```

The parameters you need to pay attention to are as follows:

#### Parameters

- **EXTERNAL_IP**: The server’s public IP. This corresponds to **Server Address** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **INTERNAL_IP**: The server’s internal IP.
- **CROSSDESK_SERVER_PORT**: The port used by the self-hosted service. This corresponds to **Server Port** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **COTURN_PORT**: The port used by the COTURN service. This corresponds to **Relay Service Port** in the CrossDesk client’s **Self-Hosted Server Configuration**.
- **MIN_PORT / MAX_PORT**: The port range used by the COTURN service. Example: `MIN_PORT=50000`, `MAX_PORT=60000`. Adjust the range depending on the number of clients.
- `-v /var/lib/crossdesk:/var/lib/crossdesk`: Persists database and certificate files on the host machine.
- `-v /var/log/crossdesk:/var/log/crossdesk`: Persists log files on the host machine.

#### Example

```bash
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=114.114.114.114 \
  -e INTERNAL_IP=10.0.0.1 \
  -e CROSSDESK_SERVER_PORT=9099 \
  -e COTURN_PORT=3478 \
  -e MIN_PORT=50000 \
  -e MAX_PORT=60000 \
  -v /var/lib/crossdesk:/var/lib/crossdesk \
  -v /var/log/crossdesk:/var/log/crossdesk \
  crossdesk/crossdesk-server:v1.1.6
```

#### Notes

- **The server must open the following ports: COTURN_PORT/udp, COTURN_PORT/tcp, MIN_PORT–MAX_PORT/udp, and CROSSDESK_SERVER_PORT/tcp.**
- If you don’t mount volumes, all data will be lost when the container is removed.
- Certificate files will be automatically generated on first startup and persisted to the host at `/var/lib/crossdesk/certs`. As the default certificates are self-signed and cannot guarantee security, it is strongly recommended to apply for a trusted certificate from a cloud provider, deploy it to this directory, and restart the service.
- The database file will be automatically created and stored at `/var/lib/crossdesk/db/crossdesk-server.db`.
- Log files will be created and stored at `/var/log/crossdesk/`.

#### Directory permissions

If the directories automatically created by Docker belong to root and have insufficient write permissions, the container user may not be able to write to them. This can cause:

- Certificate generation failure, leading to startup script errors and container exit.
- Database directory creation failure, causing the program to throw exceptions and crash.
- Log directory creation failure, preventing logs from being written (though the program may continue running).

Manually set permissions before starting the container:

```bash
sudo mkdir -p /var/lib/crossdesk /var/log/crossdesk
sudo chown -R $(id -u):$(id -g) /var/lib/crossdesk /var/log/crossdesk
```

#### TLS certificate

Place **crossdesk.cn.key** and **crossdesk.cn_bundle.crt** into the **/path/to/your/certs** directory.

### Client

1. Click the settings icon in the top-right corner to enter the settings page.<br><br>
<img width="600" height="210" alt="image" src="https://github.com/user-attachments/assets/6431131d-b32a-4726-8783-6788f47baa3b" /><br>

2. Click `Self-Hosted Server Configuration` button.<br><br>
<img width="600" height="160" alt="image" src="https://github.com/user-attachments/assets/24c761a3-1985-4d7e-84be-787383c2afb8" /><br>

3. Enter the `Server Address` (**EXTERNAL_IP**), `Signaling Service Port` (**CROSSDESK_SERVER_PORT**), and `Relay Service Port` (**COTURN_PORT**) and click OK button.

4. Check the `Self-hosted server configuration` option and click the OK button to save the settings. If the server is using a valid (official) certificate, the process ends here and the client will show that it is connected to the server.

5. If the default certificate is used (skip this step if an official certificate is used), download the self-signed root certificate `api.crossdesk.cn_root.crt` from the server directory /var/lib/crossdesk/certs/ to the machine running the client, and install the certificate by executing the following command:

**Windows:** Open PowerShell with administrator privileges and execute:

```powershell
certutil -addstore "Root" "C:\path\to\api.crossdesk.cn_root.crt"
```

**Linux:**

```bash
sudo cp /path/to/api.crossdesk.cn_root.crt /usr/local/share/ca-certificates/api.crossdesk.cn_root.crt
sudo update-ca-certificates
```

**macOS:**

```bash
sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain path/to/api.crossdesk.cn_root.crt
```

### Self-Hosted Web Client

See [CrossDesk Web Client](https://github.com/kunkundi/crossdesk-web-client).

---

## FAQ

See [FAQ](https://github.com/kunkundi/crossdesk/blob/self-hosted-server/docs/FAQ.md).

---

## Acknowledgements

- Thanks to [HelloGitHub](https://hellogithub.com/) for the recommendation and exposure.
- Thanks to [Ruanyf Weekly](https://github.com/ruanyf/weekly) for featuring CrossDesk.
- Thanks to the [LinuxDo](https://linux.do) community for the attention, discussions, and support that helped improve CrossDesk.

---

## Code signing policy

CrossDesk uses SignPath.io to code-sign official Windows releases built from this repository.

**Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).**

- **Committers and reviewers:** [kunkundi](https://github.com/kunkundi)
- **Approvers:** [kunkundi](https://github.com/kunkundi)
- **Privacy policy:** CrossDesk will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.
