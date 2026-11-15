# CrossDesk Android 使用指南

[返回首页](../../README.md) · [常见问题](../../docs/FAQ.md)

用 Android 手机或平板控制远程电脑。需要 **Android 8.0 或以上、arm64 设备**；目前不能把 Android 设备作为被控端，也不支持 32 位设备或 x86 模拟器。

## 安装并连接

1. 从 [官网](https://www.crossdesk.cn/) 或 [GitHub Releases](https://github.com/kunkundi/crossdesk/releases) 下载已签名 APK，按系统提示允许安装。
2. 在电脑上安装并运行 CrossDesk，准备其 9 位设备 ID 和密码。
3. 首次打开 Android 应用时阅读隐私说明并选择是否同意联网。连接服务器后，输入电脑 ID，点击“连接”，再输入密码。
4. 需要重连时，可勾选保存密码，以后从近期连接打开设备。电脑密码变化后按提示重新输入。

使用自建服务器时，在 **设置 → 服务器** 填写与电脑相同的主机名和端口，不要填写 `https://` 或完整网页地址。默认服务为 `api.crossdesk.cn:9099`。自建证书须被系统信任；安装或移除 CA 后重新启动应用。

## 鼠标、键盘与画面

| 操作 | 用法 |
| --- | --- |
| 移动鼠标 | 设置中选相对模式，像触控板一样滑动；绝对模式直接定位到触点 |
| 点击和拖动 | 单指点击，长按右键；绝对模式下按住拖动，也可使用虚拟鼠标的左右键 |
| 缩放和移动画面 | 双指捏合缩放；放大后双指拖动画面，原始比例下双指上下滑动滚轮 |
| 输入文字 | 从悬浮菜单打开系统键盘，确认输入法候选后发送到远端 |
| 电脑按键 | 切换到“电脑键盘”，使用功能键或组合键 |
| 多显示器 | 从悬浮菜单选择要看的显示器 |
| 画质与帧率 | 在会话画面设置中调整，旧版被控端不支持时选项不可用 |

可连接外接键盘和鼠标。手机需要保持横屏时，开启 **设置 → 屏幕方向 → 连接建立后锁定横屏**；平板和多窗口不受此开关影响。

系统输入法的中文、表情和听写需要被控端支持文本输入。不能直接输入时使用剪贴板；Windows 锁屏输入还要求电脑的客户端、服务与辅助程序版本一致。`Ctrl+Alt+Del` 需要电脑安装并运行 CrossDesk Service。

快捷键的“更多”面板提供常用、按键、系统和自定义分组。可保存最多 16 个自定义组合，并为每个远端平台固定最多 8 项。Linux 系统快捷键需选择 GNOME 或 KDE Plasma。

## 声音、隐私屏与状态

在悬浮菜单开关声音或隐私屏。隐私屏按钮显示电脑确认的状态；开启后会遮住电脑本地画面并屏蔽本地键鼠，是否可用取决于被控系统及其他连接。出现失败或不可用提示时按提示处理。

网络状态可以查看直连 / 中继、加密、流量、丢包、分辨率和帧率。“—”表示暂无有效数据。画面卡顿或手机发热时，可降低画质和帧率；硬件解码不可用时会使用软件解码。

## 剪贴板与文件

- **剪贴板：** 主动发送输入的文本，单次最多 128 KiB。收到远端文本后，点击“复制远端文本”才写入手机剪贴板。
- **发送文件：** 通过系统文件选择器选取文件，查看传输进度。
- **接收文件：** 等待接收完成后使用“保存收到的文件”导出；连续收到多个文件时逐个保存。

**Android 接收的文件在会话结束后会从临时缓存清除，需要保留时请在断开前导出。** 文件选择器打开期间会保留连接，但停留在后台超过五分钟可能结束会话。

## 切换应用与画中画

收到远程画面后切换应用，系统支持并允许时会自动进入画中画。通过小窗返回即可继续操作；关闭小窗会结束会话。

不能使用画中画时，应用尝试用带通知的前台服务保留连接。后台不播放画面和声音，可从通知返回或断开。系统强制停止、移除任务或网络中断仍可能结束连接；系统版本满足要求不代表设备一定提供画中画。

## 连接开启可信设备限制的电脑

在 **设置 → 本机配对信息** 复制配对信息，交给被控电脑。在电脑的 **管理可信设备 → 添加可信设备** 中粘贴并核对指纹后添加，再照常使用连接密码。

重启、清理缓存或更换服务器不影响配对。清除应用数据或卸载后重新安装，需要重新配对。详见 [可信设备说明](../../docs/connection-architecture.md#trusted-devices)。

## 隐私、预览与更新

**设置 → 隐私** 可查看或撤回联网授权，撤回会断开连接。保存远程画面预览默认关闭；开启后，下次连接可保存一张本地预览用于近期设备卡片，不会上传或进入相册。关闭保存或撤回授权会清除预览。

首页铃铛用于查看公告。发现新版本时，设置和关于页会显示提示，可从 **关于 → 检查更新** 手动检查。更新 APK 需使用兼容的签名；自行构建的包不一定能覆盖官网安装版。

软件许可、开源组件及对应源码信息可在 **关于 CrossDesk** 离线查看，也可阅读 [开源权利说明](licenses/OPEN_SOURCE_NOTICE.txt)。CrossDesk 使用 [GPL-3.0-only](../../LICENSE)，MiniRTC 及其他组件保留各自许可；开源组件页面包含 OpenFEC 告知与完整许可文档。

## 自行构建（可选）

已安装发布包的用户可以跳过本节。以下从仓库根目录开始；尚未下载源码时先执行：

```sh
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
```

在 macOS 或 Linux 安装：

- JDK 17 或更新版本（可使用 Android Studio 自带 JBR）。
- Android SDK Platform 36、Build Tools 36.0.0、NDK `28.2.13676358`。
- Xmake **3.1.1 或更新版本**、Python 3.9+、Git、CMake 和主机编译工具。
- Python 的 setuptools、wheel，用于构建依赖的宿主工具。

先在仓库根目录初始化子模块：

```sh
git submodule update --init --recursive
```

macOS 命令行示例：

```sh
export ANDROID_HOME="$HOME/Library/Android/sdk"
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
# 使用 PATH 中的 Xmake（最低 3.1.1），或通过 XMAKE_BIN 指定其他可执行文件。
cd apps/android
./gradlew :app:assembleDebug
```

也可用 Android Studio 打开 `apps/android`。在 SDK Manager 安装上述版本；SDK 位置可由 IDE 写入被忽略的 `local.properties`。Xmake 不在 IDE 的 PATH 中时，启动 IDE 前设置 `XMAKE_BIN`。

输出为 `app/build/outputs/apk/debug/app-debug.apk`，已由本机 Android 调试密钥签名：

```sh
"$ANDROID_HOME/platform-tools/adb" install -r app/build/outputs/apk/debug/app-debug.apk
```

Release 构建使用 `./gradlew :app:assembleRelease`，默认输出未签名 APK；发行前需配置自己的签名。Debug APK 也使用优化后的 Release 原生库，以保证软件解码性能。

首次构建会下载并编译依赖，之后会复用缓存。构建失败时先核对 SDK、NDK、Java 和 Xmake 路径。Release APK 需用自己的签名安装；保留签名密钥，后续更新需要继续使用它。

## 遇到问题

连接失败先核对电脑在线状态、服务器和密码。黑屏、锁屏输入及隐私屏问题见 [常见问题](../../docs/FAQ.md)。反馈时提供两端版本、Android 型号和系统版本、报错及复现步骤。
