# 从源码构建

[返回 README](../README.md) · [English](BUILD_EN.md)

克隆仓库后，构建和打包命令均在仓库根目录执行。构建依赖与参数以 [Xmake 配置](../apps/desktop/xmake/options.lua) 和 [CI 工作流](../.github/workflows/build.yml) 为准。`1.4.3` 仅为本地构建示例版本号；调整时请同步修改配置命令与打包参数，避免程序内版本号与安装包不一致。

## 构建依赖

- Git、[Xmake](https://xmake.io/guide/quick-start.html) 及下文对应平台的编译工具链。
- 构建脚本通过 Xmake 管理 **CMake 3.x（≥ 3.21、< 4.0）**、**Rust 1.92.0** 等依赖；首次构建需要下载工具与源码。具体约束见 [Slint 包配置](../deps/thirdparty/slint/xmake.lua)。

## Windows

安装 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载、Windows SDK 和“适用于 Windows 的 C++ Clang 编译器”组件，并安装 Xmake。Windows x64 的 libyuv 使用 clang-cl 编译，以启用 SIMD 图像转换和缩放；主程序仍使用 MSVC。在开发者 PowerShell 中执行：

```powershell
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
xmake f -c -p windows -a x64 -m release --CROSSDESK_VERSION=1.4.3 --USE_CUDA=false -y
xmake b -vy crossdesk
xmake r crossdesk
```

性能复测应使用 `release`；`debug` 保留调试信息并关闭主程序编译优化。

安装包还使用 NSIS，需将仓库中的 [nsProcess.dll](../apps/desktop/scripts/windows/nsProcess.dll) 放入 NSIS 的 `Plugins/x86-unicode` 目录，并确保 `makensis` 在 `PATH` 中。完成 Release 编译后运行：

```powershell
.\apps\desktop\scripts\windows\pkg_x64.ps1 1.4.3
```

完整流程见 [Windows 脚本](../apps/desktop/scripts/windows/pkg_x64.ps1) 和 [CI 工作流](../.github/workflows/build.yml)。手动部署时需保留运行库、两个服务辅助程序以及 `usbmmidd_v2` 驱动文件夹（构建后由 xmake 复制到输出目录），见 [Windows 服务说明](../README.md#windows-service)。

## macOS

安装 Xcode 及其命令行工具、Xmake，然后执行：

```bash
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
xmake f -c -m release --target_minver=14.0 --CROSSDESK_VERSION=1.4.3 --USE_CUDA=false -y
xmake b -vy crossdesk
xmake r crossdesk
```

当前 CI 对 Intel 与 Apple Silicon 均设置 macOS 14.0 为最低目标版本，使用的 Xcode/SDK 版本见 [CI 工作流](../.github/workflows/build.yml)。打包时按本机架构执行一个脚本：

```bash
# Apple Silicon
./apps/desktop/scripts/macos/pkg_arm64.sh 1.4.3
# Intel
./apps/desktop/scripts/macos/pkg_x64.sh 1.4.3
```

打包脚本会生成包含 Slint 运行库的 `.app` 和 `.pkg`。屏幕录制、辅助功能授权与应用路径及签名有关；测试被控能力时请使用固定路径下的应用包。

## Linux

Linux 构建支持 Ubuntu 20.04 及以上版本的 amd64 和 arm64。发布安装包以
Ubuntu 20.04/glibc 2.31 为兼容基线。以下依赖命令以 Ubuntu 20.04 为例；其他发行版或较新版本需按其软件源调整包名和工具链，复现 CI 可使用下文的构建镜像。先安装基础编译和无头运行依赖：

```bash
sudo apt-get update
sudo apt-get install -y \
  git curl unzip build-essential gcc-10 g++-10 python3-pip \
  pkg-config binutils dpkg-dev xvfb xfce4 dbus \
  libx11-dev libxext-dev libxrender-dev libxft-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev libxfixes-dev libxv-dev \
  libxtst-dev libxcb-randr0-dev libxcb-xtest0-dev libxcb-xinerama0-dev \
  libxcb-shape0-dev libxcb-xkb-dev libxcb-xfixes0-dev libxcb-shm0-dev \
  libasound2-dev libsndio-dev libpulse-dev \
  libgl1-mesa-dev
```

Ubuntu 20.04 自带的 CMake 3.16 不满足要求，Xmake 会下载符合上述范围的
CMake 3.x，无需手动替换系统 CMake。

Ubuntu 20.04 默认的 GCC 9 缺少 Slint C++ API 所需的部分 C++20 标准库，
因此上述命令同时安装并在后续配置中指定 GCC 10。

下载子模块并编译 Release 版本：

```bash
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk

# 已经克隆过仓库时执行这一行
git submodule update --init --recursive

xmake f -c -m release --CROSSDESK_VERSION=1.4.3 --USE_CUDA=false --toolchain=gcc-10 -y
xmake b -vy crossdesk
```

这里应使用 `--toolchain=gcc-10`，以确保 CrossDesk 及 xmake 在配置阶段构建的
libyuv 等第三方包都使用 GCC 10。只指定 `--cc`/`--cxx` 时，依赖安装可能仍回退到
系统默认的 `/bin/cc` 和 `/bin/c++`。`-c` 会清除旧配置，切换编译器时必须保留。

amd64 构建结果位于 `build/linux/x86_64/release/crossdesk`，arm64 构建结果位于 `build/linux/arm64/release/crossdesk`。

启用 Wayland 和 DRM 时，再安装 D-Bus、DRM 及 PipeWire 开发依赖。Ubuntu 20.04 使用仓库提供的 PipeWire 头文件 SDK；Ubuntu 22.04 及以上可改用系统的 `libpipewire-0.3-dev` 和 `libspa-0.2-dev` 包：

```bash
sudo apt-get install -y libdbus-1-dev libdrm-dev
# 已安装系统 PipeWire 0.3 开发包时跳过这一行
sudo ./docker/linux-build/install-pipewire-sdk.sh

xmake f -c -m release --USE_WAYLAND=true --USE_DRM=true --USE_CUDA=false \
  --CROSSDESK_VERSION=1.4.3 --toolchain=gcc-10 -y
xmake b -vy crossdesk
```

生成 Debian 安装包（必须在对应架构上先完成 Release 编译）：

```bash
# amd64
./apps/desktop/scripts/linux/pkg_amd64.sh 1.4.3

# arm64
./apps/desktop/scripts/linux/pkg_arm64.sh 1.4.3
```

打包脚本会将 Slint 共享运行库安装到软件包私有目录 `/usr/lib/crossdesk`，无需用户另外安装 `libslint_cpp.so`。
PipeWire 不属于强制运行时依赖：程序在运行时检测宿主系统的 PipeWire 0.3，
没有该运行库时仍可使用 X11（以及构建时启用的 DRM）捕获。

## 通用编译选项

```text
--USE_CUDA=true/false: 启用 CUDA 硬件编解码，默认不启用
--USE_WAYLAND=true/false: 在 Linux 上启用 Wayland/PipeWire 捕获，默认不启用
--USE_DRM=true/false: 在 Linux 上启用 DRM 捕获，默认不启用
--CROSSDESK_PORTABLE=true/false: 构建便携版本，默认不启用
--CROSSDESK_VERSION=xxx: 指定 CrossDesk 的版本
--MINIRTC_ENABLE_AOM=true/false: 编译可选 libaom 后端，默认不启用；AV1 仍使用 SVT-AV1 / dav1d

# 示例
xmake f --CROSSDESK_VERSION=1.4.3 --USE_CUDA=true
```

运行：

```bash
xmake r crossdesk
```

## 无 CUDA 环境下的开发支持

对于**未安装 CUDA 环境的 Linux 开发者，如果希望编译后的成果物拥有硬件编解码能力**，这里提供了预配置的 [Ubuntu 20.04 兼容构建镜像](https://hub.docker.com/r/crossdesk/ubuntu20.04)。该镜像内置必要的构建依赖，可生成兼容 glibc 2.31 的单一 Linux 安装包。

镜像版本和 amd64 / arm64 差异见 [构建镜像说明](../docker/linux-build/README.md)。以下 CUDA 示例适用于 amd64 镜像；进入容器、下载工程后执行：

```bash
export CUDA_PATH=/usr/local/cuda
export XMAKE_GLOBALDIR=/data

xmake f --root -c -m release --CROSSDESK_VERSION=1.4.3 --USE_CUDA=true --toolchain=gcc-10 -y
xmake b --root -vy crossdesk
```

对于**未安装 CUDA 环境的 Windows 开发者**，执行下面的命令安装 CUDA 编译环境：

```powershell
xmake require -vy "cuda 12.6.3"
```

安装完成后执行:

```powershell
xmake require --info "cuda 12.6.3"
```

在输出中查找 `installdir` 字段。

使用 `installdir` 指向的 CUDA 安装目录。将 CUDA_PATH 加入系统环境变量，或在终端中输入：

```powershell
$env:CUDA_PATH = "C:\path\to\cuda_installdir"
```

重新执行：

```powershell
xmake f --USE_CUDA=true
xmake b -vy crossdesk
```

### 运行时连接问题

“未连接服务器”可能由网络、服务器配置或证书信任引起，不应只通过安装另一份客户端来排查。请按 [常见问题](FAQ.md) 和 [自托管证书说明](SELF_HOSTING.md) 检查。

## 关于 Xmake

### 安装 Xmake

使用 curl：

```bash
curl -fsSL https://xmake.io/shget.text | bash
```

使用 wget：

```bash
wget https://xmake.io/shget.text -O - | bash
```

使用 powershell：

```powershell
irm https://xmake.io/psget.text | iex
```

### 编译选项

```text
# 切换到 Debug；Release 构建将 debug 替换为 release
xmake f -m debug

# 可选编译参数
-r ：重新构建目标
-v ：显示详细的构建日志
-y ：自动确认提示

# 示例
xmake b -vy crossdesk
```

### 运行选项

```bash
# 使用调试器启动当前构建；不会自动切换为 Debug 编译模式
xmake r -d crossdesk
```

更多使用方法可参考 [Xmake 官方文档](https://xmake.io/guide/quick-start.html)。

## iOS / iPadOS

原生客户端使用独立 Xcode 工程，要求 iOS 16+ 的 arm64 真机。构建与音视频、文件传输验证步骤见 [iOS 开发说明](../apps/ios/README.md)。当前 CI 导出未签名应用，真机安装需要自行签名。

## 更新界面截图

README 的桌面图片直接由当前 Slint UI 渲染，复现方式见 [截图说明](images/README.md)。
