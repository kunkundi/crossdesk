<div align="center">
  <img src="docs/images/crossdesk-banner.svg" width="420" alt="CrossDesk 标志：图标在左，双色文字在右" />
  <p>轻量、跨平台的远程桌面，支持电脑、浏览器和移动端连接，用桌面墙集中查看与操控多台设备。</p>
  <p><a href="https://www.crossdesk.cn/">官方网站</a> · <a href="https://github.com/kunkundi/crossdesk/releases">下载客户端</a> · <a href="https://web.crossdesk.cn/">打开 Web 客户端</a> · <a href="README_EN.md">English</a></p>
</div>

<div align="center">

[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux%20%7C%20iOS%20%7C%20Android%20%7C%20HarmonyOS-blue?style=flat)](#download)

[![Release](https://img.shields.io/github/v/release/kunkundi/crossdesk?style=flat)](https://github.com/kunkundi/crossdesk/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/kunkundi/crossdesk/build.yml?style=flat&label=build)](https://github.com/kunkundi/crossdesk/actions/workflows/build.yml)
[![License: GPL v3](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat)](LICENSE)
[![Stars](https://img.shields.io/github/stars/kunkundi/crossdesk?style=flat&label=stars)](https://github.com/kunkundi/crossdesk/stargazers)

[![爱发电](https://img.shields.io/badge/%E7%88%B1%E5%8F%91%E7%94%B5-946CE6?logo=afdian&logoColor=white)](https://afdian.com/a/kunkundi)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-FF6433?logo=kofi&logoColor=white)](https://ko-fi.com/kunkundi)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub_Sponsors-C93C45?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/kunkundi)

</div>

CrossDesk 让你从电脑、浏览器或手机连接远端电脑，也能通过 **桌面墙（DeskHub）** 集中查看与操控多台设备。项目基于 [MiniRTC](https://github.com/kunkundi/minirtc)，支持自托管。

[主要功能](#features) · [下载与平台支持](#download) · [快速连接](#quick-start) · [IP 直连](#lan-direct) · [界面预览](#preview) · [使用指南](docs/USER_GUIDE.md) · [自托管](#self-hosting) · [构建与反馈](#development) · [赞助](#sponsor)

<a id="features"></a>

## 主要功能

- **跨平台远控：** Windows、macOS、Linux 桌面互控，浏览器及原生 iOS / Android 客户端访问电脑；鸿蒙 PC 与移动端处于开发阶段。
- **局域网 IP 直连：** 桌面客户端通过 IP、端口和六位密码连接，无需云端信令服务；支持独立端口、记住密码及按模式显示近期设备。
- **桌面墙：** 多设备实时预览、分组、放大操控、群控和批量文件分发。
- **画面与声音：** H.264 / AV1、硬件编解码、远端音频，以及会话内画质和帧率调整；可用能力随平台与设备而异。
- **日常协作：** 多显示器切换、键鼠与组合键、文本剪贴板、文件传输。
- **部署与运维：** P2P 直连、TURN 中继、SRTP 媒体加密、自托管，以及 Windows 登录前远控和 Linux 无头运行。

<a id="download"></a>

## 下载与平台支持

从 [官方网站](https://www.crossdesk.cn/) 或 [GitHub Releases](https://github.com/kunkundi/crossdesk/releases) 下载与你的系统和 CPU 架构匹配的安装包；也可直接打开 [Web 客户端](https://web.crossdesk.cn/)。

| 平台 | 支持范围与安装要求 |
| --- | --- |
| Windows | **控制 / 被控**；Windows 10+，x64；`.exe` 安装包，也支持便携构建 |
| macOS | **控制 / 被控**；macOS 14.0+；按 Intel / Apple Silicon 选择 x64 / arm64 `.pkg` |
| Linux | **控制 / 被控**；Ubuntu 20.04+、glibc 2.31 基线；amd64 / arm64 `.deb` |
| iOS / iPadOS | **仅控制**；iOS 16.0+；发布包为未签名应用，需自行签名，或使用 Xcode 构建安装 |
| Android | **仅控制**；Android 8.0+，arm64；下载已签名 APK 安装，或自行构建 |
| 鸿蒙 PC（开发版） | **控制 / 被控**；API 26，arm64；使用 DevEco 构建并签名 HAP，仍在进行真机验收 |
| 鸿蒙手机 / 平板（开发版） | **仅控制**；API 20+，arm64；使用 DevEco 构建并签名 HAP，仍在进行真机验收 |
| Web | **仅控制**；支持 WebRTC 的浏览器；无需在控制端安装客户端 |

系统要求按当前源码与构建配置整理，具体安装包以对应 Release 说明为准。各客户端的功能范围见 [移动端与鸿蒙](#mobile)。

- **Linux：** 已有桌面选普通版 `crossdesk-linux-…deb`；需要独立 Xfce 桌面的服务器选 `crossdesk-virtual-desktop-xfce-…deb`。两者选择其一，安装与启动方法见 [使用指南](docs/USER_GUIDE.md#installation) 和 [无头运行](docs/HEADLESS.md)。
- **macOS：** 首次运行时按提示授予屏幕录制及辅助功能权限；前者用于桌面采集，后者用于远程键鼠输入。[权限说明](docs/USER_GUIDE.md#installation)
- **iOS / 鸿蒙：** 签名与构建步骤分别见 [iOS](apps/ios/README.md) 和 [鸿蒙](apps/harmony/README.md)，不要将未签名产物当作可直接安装的应用。

<a id="quick-start"></a>

## 快速连接

### 通过设备 ID 连接

1. **准备被控电脑。** 安装并运行 CrossDesk，切换至 ID 连接模式，等待显示“已连接服务器”，查看“本桌面”中的本机 ID 和密码。
2. **发起连接。** 在另一台电脑的 CrossDesk 中输入被控电脑的 9 位 ID，点击“连接”；也可使用原生移动端或 [Web 客户端](https://web.crossdesk.cn/)。
3. **验证密码。** 输入被控电脑当前的连接密码，等待远程画面出现。桌面端可勾选“记住密码”，后续从“近期连接”重连。

使用自建服务器时，两端需要接入同一服务。更多连接、密码及浏览器操作见 [使用指南](docs/USER_GUIDE.md#quick-start)；连接失败、黑屏或无法操作键鼠时先查阅 [常见问题](docs/FAQ.md)。

<a id="lan-direct"></a>

### 通过局域网 IP 直连

1. **切换模式。** 两台电脑均运行支持 IP 直连的桌面客户端，点击左上角模式按钮切换到 **IP 直连模式**。若按钮未显示，在 **设置 → 常规 → 网络连接** 中勾选 **启用 IP 直连** 并保存。
2. **填写地址。** 在控制端输入被控电脑显示的 `IP:端口`，例如 `192.168.1.20:52888` 或 `[2001:db8::20]:52888`，点击“连接”。只输入 IP 时使用默认端口 **52888**；请以对方界面显示的实际端口为准。
3. **输入密码。** 输入被控电脑在 IP 直连模式下显示的 **6 位英文字母或数字密码**，可勾选“记住密码”。切换模式时，“近期连接”同步切换为对应模式的设备记录。

两台电脑需要在局域网内可互相访问；网络可以有外网，也可以没有外网，直连会话不依赖云端信令服务。主窗口可直接切换本机 IP、修改端口和密码，默认开启所有 IP 接口监听，支持 IPv4 和 IPv6，支持通过 VPN 等接口连接，可在设置的“网络连接”中调整；关闭设置中的“启用 IP 直连”会停止监听、断开 IP 直连会话并隐藏模式按钮。详见 [IP 直连使用指南](docs/USER_GUIDE.md#lan-direct)。

<a id="preview"></a>

## 界面预览

### 桌面客户端

本机凭据、远程连接和近期设备集中在主窗口，左上角提供连接模式切换和桌面墙入口。

<p align="center">
  <img src="docs/images/desktop-main-zh.png" width="800" alt="CrossDesk ID 连接模式：模式切换、本机 ID、远程连接入口和近期设备；数据为演示" />
</p>

### IP 直连模式

切换本机 IP、修改端口与密码，通过对方地址连接；下方展示独立的局域网近期设备。

<p align="center">
  <img src="docs/images/desktop-lan-zh.png" width="800" alt="CrossDesk IP 直连模式：本机 IP 和端口编辑入口、六位密码、远端地址输入和局域网近期设备；数据为演示" />
</p>

<a id="deskhub"></a>

### 桌面墙（DeskHub）

按分组查看多台设备，点击画面放大，再选择接管操控；支持批量连接、静音与文件分发。

<p align="center">
  <img src="docs/images/desktop-deskhub-zh.png" width="960" alt="DeskHub 桌面墙：六台演示设备的远程画面与管理工具栏" />
</p>

群控会向所有已连接设备发送输入；关闭桌面墙会断开全部远程连接。操作范围与详细用法见 [桌面墙使用指南](docs/USER_GUIDE.md#deskhub)。截图使用演示数据，更多界面及来源见 [使用指南](docs/USER_GUIDE.md) 与 [截图说明](docs/images/README.md)。

<a id="session"></a>
<a id="settings"></a>

## 会话操作与设置

远程会话支持显示器切换、画质与帧率调整、声音、文本剪贴板同步和文件传输。工具栏还可查看网络状态、控制隐私屏；桌面设置中可配置 IP 直连开关、编码、中继、自托管与桌面墙画面预算。

- [会话操作](docs/USER_GUIDE.md#session)：工具栏、文本剪贴板、文件保存和隐私屏。
- [客户端设置与更新](docs/USER_GUIDE.md#settings)：常规设置、桌面墙传输及版本检查。
- [桌面墙技术说明](docs/deskhub.md)：预览订阅、带宽分配、协议与兼容性。

<a id="mobile"></a>

## 移动端与鸿蒙

<a id="ios"></a>

### iOS / iPadOS

原生控制端支持触控与虚拟鼠标、多显示器、远端音频、文件收发和文本剪贴板。悬浮菜单的“剪贴板”按钮可主动发送本机文本，单次上限为 128 KiB。iPhone / iPad 当前不能作为被控桌面。

[使用说明](docs/USER_GUIDE.md#ios) · [构建与签名](apps/ios/README.md)

<a id="android"></a>

### Android

原生控制端支持触控与外接键鼠、多屏切换、文件收发、文本剪贴板和会话画面设置。H.264 / AV1 优先使用硬件解码，不支持或运行失败时回退到软件解码；切换应用时优先通过画中画保持会话，画中画不可用时尝试前台服务保活。当前不提供手机被控功能。悬浮菜单中的“隐私屏”按钮可直接开启或关闭电脑本地遮罩，按钮按电脑确认的状态显示“隐私屏已开启”或“隐私屏已关闭”，开启与关闭使用相同配色，正常切换不显示额外文字，仅在异常时显示提示；开启时与桌面端一致屏蔽电脑本地键鼠。

[使用说明](docs/USER_GUIDE.md#android) · [源码构建](apps/android/README.md)

<a id="harmonyos"></a>

### 鸿蒙 HarmonyOS（开发版）

PC 端可控制和被控制，复用桌面界面与桌面墙；手机和平板使用原生 ArkUI 控制端，支持触控、外接键鼠、多屏、文本剪贴板和文件收发。PC 录屏与输入控制需要系统授权，真机和长期会话验收仍在进行。

[使用与限制](docs/USER_GUIDE.md#harmonyos) · [构建与签名](apps/harmony/README.md) · [PC 说明](apps/harmony/desktop-port/README.md) · [移动端功能](apps/harmony/MOBILE_PARITY.md)

<a id="windows-service"></a>

## 无人值守与无头运行

- **Windows：** CrossDesk Service 支持锁屏、登录和安全桌面控制。安装版可额外启用登录前远控，使用独立设备 ID 和密码；普通“开机自启”仅在用户登录后启动客户端。功能限制及启停命令见 [Windows 无人值守指南](docs/USER_GUIDE.md#windows-service)。
- **Linux：** 可复用现有桌面，也可创建独立 Xvfb / Xfce 会话，操作方法见 [Linux 无头运行](docs/HEADLESS.md)。

<a id="self-hosting"></a>

## 自托管

可自行部署信令与 TURN 中继服务，并在客户端“设置 → 常规 → 自托管配置”中启用。控制端与被控端需要接入同一服务。

[部署指南](docs/SELF_HOSTING.md) · [客户端配置](docs/USER_GUIDE.md#self-hosting) · [服务端源码](https://github.com/kunkundi/crossdesk-server) · [Web 客户端部署](https://github.com/kunkundi/crossdesk-web-client)

<a id="development"></a>

## 构建与反馈

- [终端客户端 CLI](apps/cli/README.md)：ANSI / Kitty 远程桌面观看、可选键鼠控制和 Pi 扩展；支持独立于桌面 GUI 的构建，npm 分发尚未发布。

- [源码构建](docs/BUILD.md)：桌面端依赖、编译参数与打包；移动端请使用上方各平台的构建指南。
- [GUI 架构](docs/gui-architecture.md)：桌面界面、平台实现与工程结构。
- [连接模式架构](docs/connection-architecture.md)：ID 连接与 IP 直连的寻址、认证、协商、数据路径及生命周期。
- [常见问题](docs/FAQ.md)：连接失败、黑屏、系统权限、文件位置与构建问题。
- [提交问题或建议](https://github.com/kunkundi/crossdesk/issues)：请提供两端系统、客户端版本、连接方式和可复现步骤。

桌面端的 `CROSSDESK_UPDATE_PUBLIC_KEY` 是可选编译参数，用于验证应用内升级清单的 Ed25519 签名。不配置仍可正常编译、运行和检查版本，但更新只能手动下载；需要应用内自动安装更新时，须配置与发布私钥匹配的公钥。配置方式见 [更新公钥](docs/BUILD.md#update-public-key)，密钥生成与发布流程见 [桌面端应用内升级](docs/UPDATER.md)。

<a id="sponsor"></a>

## 支持 CrossDesk

如果 CrossDesk 对你有帮助，欢迎通过以下平台赞助，支持项目的持续开发、跨平台测试与日常维护。感谢你的支持！

[爱发电](https://afdian.com/a/kunkundi) · [Ko-fi](https://ko-fi.com/kunkundi) · [GitHub Sponsors](https://github.com/sponsors/kunkundi)

## 致谢与许可

感谢 [HelloGitHub](https://hellogithub.com/)、[阮一峰的科技爱好者周刊](https://github.com/ruanyf/weekly) 与 [LinuxDo](https://linux.do) 社区的推荐、交流与支持。

Copyright (c) 2026 JUNKUN DI.

CrossDesk 使用 [GPL-3.0-only](LICENSE) 许可。隐私说明见 [隐私政策](PRIVACY.md#中文)。

### Code signing policy

CrossDesk 使用 SignPath.io 为本仓库构建的官方 Windows 发布包提供代码签名。

**Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).**

- **Committers and reviewers（代码提交与审核）：** [kunkundi](https://github.com/kunkundi)
- **Approvers（签名审批）：** [kunkundi](https://github.com/kunkundi)
