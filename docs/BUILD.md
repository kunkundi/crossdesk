# 从源码构建

[返回 README](../README.md) · [English](BUILD_EN.md)

安装版的升级器随主程序构建。发布公钥、清单签名和验证方法见
[桌面端应用内升级](UPDATER.md)；便携版不包含升级器。

克隆仓库后，构建和打包命令均在仓库根目录执行。构建依赖与参数以 [Xmake 配置](../apps/desktop/xmake/options.lua) 和 [CI 工作流](../.github/workflows/build.yml) 为准。`1.4.3` 仅为本地构建示例版本号；调整时请同步修改配置命令与打包参数，避免程序内版本号与安装包不一致。

## 构建依赖

默认只构建桌面客户端。终端主控可通过 `--CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y` 单独构建；同时开启两个选项可构建两个客户端。CLI 的依赖准备、运行和 npm 打包见 [终端客户端](../apps/cli/README.md)。

- Git、[Xmake](https://xmake.io/guide/quick-start.html)（桌面端和移动端均要求 3.1.1 或更新版本）及下文对应平台的编译工具链。
- 构建脚本通过 Xmake 管理 **CMake 3.x（≥ 3.21、< 4.0）**、**Rust 1.92.0** 等依赖；首次构建需要下载工具与源码。具体约束见 [Slint 包配置](../deps/thirdparty/slint/xmake.lua)。

鸿蒙 PC 的 Slint 移植直接编译桌面公共界面和业务代码，见 [Slint 桌面构建与一致性验收](../apps/harmony/desktop-port/README.md)（API 26）。独立 ArkUI 手机 / 平板工程见 [HarmonyOS 构建与真机验证](../apps/harmony/README.md)。请使用各自说明中的脚本，不要通过桌面根工程切换鸿蒙配置。

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

### Developer ID 签名与公证

默认打包对应用使用 ad-hoc 签名，不签名安装包，适用于本地开发。对外分发时，需要在钥匙串安装同一团队的 `Developer ID Application`、`Developer ID Installer` 证书及对应私钥，并设置以下环境变量（替换证书名称与团队 ID）：

```bash
export CROSSDESK_MACOS_SIGN_IDENTITY='Developer ID Application: Your Organization (TEAMID)'
export CROSSDESK_MACOS_INSTALLER_IDENTITY='Developer ID Installer: Your Organization (TEAMID)'

# 仅首次配置：按提示输入 Apple ID 和 App 专用密码，保存到本机钥匙串。
xcrun notarytool store-credentials crossdesk-notary --team-id TEAMID
export CROSSDESK_MACOS_NOTARY_PROFILE=crossdesk-notary

# 先完成上面的 release 编译；Intel 改用 pkg_x64.sh。
./apps/desktop/scripts/macos/pkg_arm64.sh 1.4.3
```

脚本依次签署 Slint 动态库、更新器与应用，启用 Hardened Runtime、安全时间戳及所需资源权限，再使用 Installer 证书签署 `.pkg`。配置公证 Profile 后会提交 Apple 公证，仅在返回 `Accepted` 后附加票据并校验 Gatekeeper。未配置 Profile 时只生成已签名但未公证的安装包；签名与公证不代替用户对屏幕录制、辅助功能等权限的授权。

已有签名安装包可以单独提交公证：

```bash
CROSSDESK_MACOS_NOTARY_PROFILE=crossdesk-notary \
  bash apps/desktop/scripts/macos/notarize_pkg.sh crossdesk-macos-arm64-1.4.3.pkg
```

上传成功后会立即显示提交 ID，并保存 `.notary-submit.json`；公证结果及日志保存在安装包旁的 `.notary.json`、`.notary-log.json` 文件中。失败或超时会终止打包并保留产物及提交 ID。默认等待 30 分钟，可用 `CROSSDESK_MACOS_NOTARY_TIMEOUT` 调整。超时后可用 `xcrun notarytool wait <提交ID> --keychain-profile crossdesk-notary` 继续等待，确认 `Accepted` 后执行 `xcrun stapler staple <安装包>`、`xcrun stapler validate <安装包>` 和 `spctl --assess --type install --verbose=2 <安装包>`。

若加速上传出现连接超时，可设置 `CROSSDESK_MACOS_NOTARY_S3_ACCELERATION=false`，使用普通 S3 上传通道重试；默认值为 `true`。上传失败也可能已在 Apple 端创建记录，重试前先用 `xcrun notarytool history --keychain-profile crossdesk-notary` 检查提交历史。

自定义钥匙串可通过 `CROSSDESK_MACOS_KEYCHAIN` 指定，证书私钥与公证 Profile 都需要存放在该钥匙串。不要将私钥、P12 文件或密码提交到仓库。苹果说明：[Developer ID 证书](https://developer.apple.com/help/account/certificates/create-developer-id-certificates/)、[公证要求](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)。

### GitHub Actions 自动签名与公证

[Build and Release 工作流](../.github/workflows/build.yml) 对 `v*` 发布标签自动启用 macOS 签名、公证与票据附加；手动运行时可勾选 `sign_macos`。普通分支推送及未勾选的手动构建沿用 ad-hoc 签名。两种架构均在各自 runner 上完成处理，只有公证及 Gatekeeper 校验成功的签名构建才上传正式产物；缺少 Secrets 或公证失败会阻止发布。

先在本机“钥匙串访问 → 我的证书”中，分别将 `Developer ID Application` 和 `Developer ID Installer` **连同对应私钥**导出为两个 `.p12` 文件，并为这两个文件设置相同的导出密码。只有 `.cer` 证书文件不能用于 CI 签名。文件保存在工程之外，例如 `~/Downloads/crossdesk-application.p12` 和 `~/Downloads/crossdesk-installer.p12`。

在 GitHub 仓库 **Settings → Secrets and variables → Actions → New repository secret** 中配置：

| Secret | 内容 |
| --- | --- |
| `MACOS_APP_CERTIFICATE_P12_BASE64` | Application 证书及私钥的 `.p12` 文件的 Base64 |
| `MACOS_INSTALLER_CERTIFICATE_P12_BASE64` | Installer 证书及私钥的 `.p12` 文件的 Base64 |
| `MACOS_P12_PASSWORD` | 导出两个 `.p12` 时设置的共同密码 |
| `APPLE_ID` | 拥有该开发团队权限的 Apple 账户邮箱 |
| `APPLE_APP_SPECIFIC_PASSWORD` | 为公证生成的 App 专用密码 |
| `APPLE_TEAM_ID` | 两张证书所属的 10 位团队 ID |

在本机分别执行以下命令，将剪贴板内容粘贴到对应的 Base64 Secret。先保存第一项，再执行第二条命令：

```bash
base64 -i "$HOME/Downloads/crossdesk-application.p12" | pbcopy
base64 -i "$HOME/Downloads/crossdesk-installer.p12" | pbcopy
```

本机保存的 `crossdesk-notary` 钥匙串 Profile 不会同步到 GitHub runner。工作流使用上述 Secrets 在临时钥匙串中重建 Profile，自动选择与团队 ID 匹配的有效证书，配置签名工具无需交互访问这些临时私钥，并在任务结束时删除证书和公证凭据。无需额外设置钥匙串密码 Secret，也无需为当前 Developer ID 分发流程配置 provisioning profile。

配置后先手动运行工作流：填写 `desktop_version`，勾选 `sign_macos`，确认 ARM64、Intel 的 `Notarize and staple macOS installer` 步骤均成功，再推送发布标签。需要先将工作流和配套脚本提交、推送到 GitHub，新的开关和步骤才会生效。

CI 最多等待公证 60 分钟；若 Apple 仍在处理，任务失败并阻止发布，但会保留 `macos-pending-notarization-*` 安装包及 `macos-notarization-*` JSON 诊断产物 14 天。下载产物后，按上一节用已保存的提交 ID 查询并附加票据，无需重复上传同一个包。不要将 pending 产物当作已经完成公证的发布包。自动签名流程不改变安装器当前的权限重置逻辑；辅助功能授权是否保留还需单独调整安装脚本。

## Linux

Linux 构建支持 Ubuntu 20.04 及以上版本的 amd64 和 arm64。发布安装包以
Ubuntu 20.04/glibc 2.31 为兼容基线。以下依赖命令以 Ubuntu 20.04 为例；其他发行版或较新版本需按其软件源调整包名和工具链，复现 CI 可使用下文的构建镜像。先安装基础编译依赖：

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

每次打包从同一份编译产物生成普通版 `crossdesk-linux-<arch>-<version>.deb` 和虚拟桌面版 `crossdesk-virtual-desktop-xfce-linux-<arch>-<version>.deb`。两者都包含完整程序，只有后者强依赖 Xvfb、Xfce 和 D-Bus 会话工具；编译和打包本身不需要安装这些组件。源码运行时如需创建完整虚拟桌面，请按 [无头运行说明](HEADLESS.md) 准备可选依赖。项目构建镜像和 CI 可保留这些组件用于测试。

打包脚本会将 Slint 共享运行库安装到软件包私有目录 `/usr/lib/crossdesk`，无需用户另外安装 `libslint_cpp.so`。同目录的 `package-name` 标记由安装包管理，用于选择更新下载，不写入用户配置。
PipeWire 不属于强制运行时依赖：程序在运行时检测宿主系统的 PipeWire 0.3，
没有该运行库时仍可使用 X11（以及构建时启用的 DRM）捕获。

## 通用编译选项

```text
--USE_CUDA=true/false: 启用 CUDA 硬件编解码，默认不启用
--USE_WAYLAND=true/false: 在 Linux 上启用 Wayland/PipeWire 捕获，默认不启用
--USE_DRM=true/false: 在 Linux 上启用 DRM 捕获，默认不启用
--CROSSDESK_PORTABLE=true/false: 构建便携版本，默认不启用
--CROSSDESK_VERSION=xxx: 指定 CrossDesk 的版本
--CROSSDESK_UPDATE_PUBLIC_KEY=xxx: 可选的 Ed25519 更新验签公钥，64 个小写十六进制字符；默认为空，不启用自动安装更新
--MINIRTC_ENABLE_AOM=true/false: 编译可选 libaom 后端，默认不启用；AV1 仍使用 SVT-AV1 / dav1d

# 示例
xmake f --CROSSDESK_VERSION=1.4.3 --USE_CUDA=true
```

运行：

```bash
xmake r crossdesk
```

<a id="update-public-key"></a>

### 更新公钥（可选）

`CROSSDESK_UPDATE_PUBLIC_KEY` 在编译时嵌入桌面客户端，用于验证升级清单的 Ed25519 签名。本地开发可以省略：程序仍可正常编译、运行和检查新版本，但只提供手动下载入口，不执行应用内自动安装。

需要应用内自动安装更新时，在对应平台的 `xmake f` 配置命令中加入此参数，再编译。例如，已完成平台配置后执行（将占位符替换为实际公钥）：

```sh
xmake f --CROSSDESK_UPDATE_PUBLIC_KEY="<64位小写十六进制公钥>"
xmake b crossdesk
```

也可以在构建前设置同名环境变量；Xmake 配置值优先于环境变量。公钥必须是 32 字节 Ed25519 公钥的十六进制表示，共 64 个小写字符；非空但格式不正确时，构建配置会报错。发布清单必须由对应私钥签名，并满足安装位置等升级条件；便携版始终使用手动下载。

GitHub Actions 构建从仓库 Actions variable `CROSSDESK_UPDATE_PUBLIC_KEY` 读取公钥。私钥仅供发布端签名使用，无需提供给普通构建。密钥生成、发布端配置及升级条件见 [桌面端应用内升级](UPDATER.md)。

## 无 CUDA 环境下的开发支持

对于**未安装 CUDA 环境的 Linux 开发者，如果希望编译后的成果物拥有硬件编解码能力**，这里提供了预配置的 [Ubuntu 20.04 兼容构建镜像](https://hub.docker.com/r/crossdesk/ubuntu20.04)。该镜像内置必要的构建依赖，可生成兼容 glibc 2.31 的两种 Linux 安装包。

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

[iOS CI](../.github/workflows/build-ios.yml) 可独立手动运行，并由主构建工作流调用；相关 Pull Request 也会触发。工具链、许可证及隐私检查、应用验证和 ZIP 打包沿用现有流程，标签发布继续收集同名的未签名应用归档。

## 更新界面截图

README 的桌面图片直接由当前 Slint UI 渲染，复现方式见 [截图说明](images/README.md)。

## Android

原生控制端位于独立的 `apps/android` Gradle 工程，要求 Android 8.0+ arm64 设备，使用 SDK 36、NDK r28c 和 Xmake 3.1.1 或更新版本。构建、APK 安装与模拟器测试见 [Android 开发说明](../apps/android/README.md)。

[Android CI](../.github/workflows/build-android.yml) 可独立手动运行，并由主构建工作流调用。CI 提供 Debug / 设备测试 APK、未签名 Release APK 和 Lint 报告，执行原生协议测试及 16 KB 对齐、许可证检查；标签发布收集未签名 Release APK，安装前需要自行签名。设备测试需要另行在 arm64 手机或模拟器上执行。
