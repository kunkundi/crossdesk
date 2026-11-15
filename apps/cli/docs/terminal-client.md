# 终端客户端使用指南

[返回 CLI 首页](../README.md) · [npm 安装与排障](npm-distribution.md) · [兼容性](validation.md)

已通过 npm 安装的用户可以直接看 [连接选项](#连接与身份)。下面的源码步骤仅供需要自行构建的用户使用。

## 构建与安装

要求 Git、Xmake >= 3.1.1、C++17 编译器、系统 zlib 头文件/链接库，以及 MiniRTC 包构建使用的系统工具。macOS SDK 已提供 zlib；Linux 需相应开发包（如 zlib1g-dev）；Windows 使用 Visual Studio C++ 构建工具，zlib 由 Xmake 获取并静态链接，运行库使用 MT。依赖需要可访问 Xmake 包仓库。Linux 使用 GCC/Clang；macOS 使用 Xcode 命令行工具。无需安装 Slint、SDL、X11、PulseAudio 或桌面采集 SDK。

先下载包含子模块的源码；已有仓库可跳过克隆，在仓库根目录执行后续命令。

```sh
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
```

```sh
./apps/cli/scripts/prepare-deps.sh
xmake f -y --CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y --USE_CUDA=n -m release
xmake build crossdesk-cli
xmake run crossdesk-cli doctor --json
```

`prepare-deps.sh` 按主仓库记录的提交初始化 MiniRTC 子模块，可重复执行。仓库默认开启桌面构建、关闭 CLI。上述参数只构建 CLI；需要同时构建桌面和 CLI 时，将 `CROSSDESK_BUILD_DESKTOP` 设为 `y`。恢复默认桌面配置使用 `xmake f -y --CROSSDESK_BUILD_DESKTOP=y --CROSSDESK_BUILD_CLI=n`。所有命令均从仓库根目录执行。

构建产物在 `build/<platform>/<arch>/<mode>/crossdesk-cli`。可把对应目录加入 PATH 或复制可执行文件到自己的 bin 目录。Pi 需要直接启动该可执行文件，不能使用会输出构建日志的 `xmake run` 作为桥接程序。

Linux 控制端无需 DISPLAY / WAYLAND_DISPLAY；被控电脑仍需可用的图形会话。

Windows 仅支持 x64，产物为 `build/windows/x64/release/crossdesk-cli.exe`；先在 Git Bash 中执行 `sh apps/cli/scripts/prepare-deps.sh`，然后在开发者 PowerShell 中执行上述 Xmake 命令，配置时额外指定 `-p windows -a x64`。在 Git Bash 中构建时同样必须指定 `-p windows`，避免被识别为 MSYS 平台。Windows 使用 Console / ConPTY 的原生键盘、鼠标事件和 VT 输出，保存并恢复控制台模式与代码页；无需 WSL。Windows Terminal、现代 PowerShell / CMD 和 VS Code 的 ConPTY 终端属于目标运行环境，仍需实机验收。Git Bash 的独立 mintty 窗口没有原生控制台时，应从 Windows Terminal 的 Git Bash 配置启动。

预编译 npm 包的用户无需构建工具，见 [npm 分发](npm-distribution.md)。

## 连接与身份

```sh
crossdesk-cli 123456789
crossdesk-cli 123456789 --renderer ansi --zoom native
crossdesk-cli 123456789 --renderer kitty --quality high --display 0
crossdesk-cli 123456789 --control
crossdesk-cli 123456789 --server your-server.example --port 9099
```

将示例换成被控电脑的 9 位 ID，保留前导零。密码在终端中隐藏输入，不提供密码命令行参数。`view <设备ID>` 也可用于连接。CLI 不修改桌面客户端的配置。

自托管服务器需要支持 CLI 临时身份。每次连接使用临时身份，断线后重新运行命令连接，不会自动反复尝试密码。默认连接超时为 30 秒。

| 选项 | 用途 |
| --- | --- |
| `--renderer auto\|ansi\|kitty\|sixel` | 默认自动选择；ANSI 是字符画面，Kitty / Sixel 是像素画面 |
| `--zoom native` | 从局部细节视图开始；`--zoom 2` 为全景的两倍 |
| `--quality low\|balanced\|high\|ultra` | 请求最高 360p / 720p / 1080p / 2160p，默认 high，实际由被控端决定 |
| `--fps 8` | 指定 1–30 FPS 的输出目标；默认本地目标为 30，实际受终端与网络影响 |
| `--display 1` | 选择第二个显示器，索引从 0 开始 |
| `--control` | 连接后直接启用键鼠控制；默认只读 |
| `--server 地址 --port 端口` | 指定自托管服务器，两端需要使用同一服务 |

状态栏会区分连接服务器、连接设备、等待画面和正在显示。已经连接但等待首帧时，先确认被控电脑有可采集的桌面及系统权限。

## ANSI、Kitty、Sixel 与 SSH

默认 `auto` 优先选择 Kitty，其次 Sixel，最后 ANSI。显式指定像素模式但终端不支持时会报错，此时使用 `--renderer ansi`。tmux / screen 使用 ANSI，SSH 需分配 PTY（`ssh -t`）。

ANSI 用彩色字符表示桌面，整屏小字不一定可读。可以扩大终端、缩小字体，或用 `--zoom native` 查看局部。遇到字体显示问题时，可用 `--ansi-detail half` 切换字符样式。

画面比例默认自动检测。若被压窄或拉宽，可设置 `--cell-aspect 0.45`，它表示字符宽度除以高度，允许 0.2–2。例如字符为 9×20 像素时使用 0.45；被压窄时调小，被拉宽时调大。

顶部 FPS 表示实际输出到终端的画面数量，静止的 ANSI 画面可能显示 0 FPS。提高视频画质或目标帧率不能突破终端本身的显示速度。

## 输入与退出

默认只读、静音。只读时按 `c` 开启输入。**控制时要退出，先按下并松开 `Ctrl+]`，再按 `q`**。

| 操作 | 只读模式 | 控制模式 |
|---|---|---|
| 开启／关闭远端控制 | `c` | `Ctrl+]`，再按 `c` |
| 退出 CLI | `q` 或 `Ctrl+C` | `Ctrl+]`，再按 `q` |
| 进入本地命令模式 | `?` 或 `Ctrl+]` | `Ctrl+]` |
| 放大／缩小 | `+` / `=` / `-` | `Ctrl+]`，再按对应键 |
| 恢复完整画面 | `f`，重置倍率、平移和细节模式 | `Ctrl+]`，再按 `f` |
| 鼠标查看 | 滚轮以鼠标位置缩放；点击进入原始细节 | 进入本地命令模式后仅更新缩放锚点，使用缩放键调整 |
| 平移 | 方向键或 `i` 上、`j` 左、`k` 下、`l` 右 | `Ctrl+]`，再按对应键 |

本地命令模式会暂停远端输入，缩放和平移后仍保持该模式。按 `c` 选择返回后是否继续控制，再按 `Esc` 或 `Ctrl+C` 返回。控制模式中的普通 `c`、`q`、`Ctrl+C` 和 `Esc` 会发送给远端。

窗口失焦后会退回只读，回到窗口时需再次按 `c`。鼠标缩放以指针位置为中心，没有有效鼠标位置时使用画面中心。

macOS Terminal 需勾选 **显示 → 允许鼠标报告**。状态栏的 `Mouse RX`（窄窗口为 `M:`）可帮助检查：移动或点击时计数一直为 0，通常表示终端未提供鼠标事件。

文本与粘贴需要被控端支持文本输入。终端不保证持续按键、所有组合键和输入法行为与桌面端相同。正常退出会恢复终端；异常终止后显示失常时可在 shell 运行 `reset`。

## Pi 扩展

扩展按 `@earendil-works/pi-coding-agent` / `pi-tui` 1.4.0 接口适配，旧 `@mariozechner` 包未确认兼容。先构建原生 CLI，然后在仓库根目录启动，例如 macOS Apple Silicon：

```sh
export CROSSDESK_CLI="$PWD/build/macosx/arm64/release/crossdesk-cli"
pi -e ./apps/cli/plugins/pi/index.ts
```

其他平台把 `CROSSDESK_CLI` 换成原生程序的绝对路径，不要填写 `xmake run`。在 Pi 中输入 `/crossdesk` 或 `/crossdesk 123456789`，按遮蔽提示输入密码。

Pi 使用 ANSI 画面，按键模式与上文一致。它需要交互式 TUI；非交互模式不能提供内嵌画面。自托管配置目前使用独立 CLI，插件没有服务器设置页。

需要自动加载时，将整个 `apps/cli/plugins/pi` 复制到 `~/.pi/agent/extensions/crossdesk/`，重启 Pi 或执行 `/reload`。卸载时关闭视图、删除该目录并重新加载。画面比例可通过 `CROSSDESK_CELL_ASPECT=0.45` 调整，`auto` 恢复自动检测。

## 故障排查与边界

| 提示或现象 | 处理方式 |
| --- | --- |
| `incorrect_password` | 重新连接并输入正确密码 |
| `device_not_found` / `device_offline_or_unavailable` | 核对 ID、服务器及电脑在线状态 |
| `signaling_login_failed` / `cli_identity_not_supported` | 检查网络和服务端是否支持 CLI 连接 |
| `tls_certificate_error` | 检查系统时间、证书有效期、主机名和系统信任 |
| `connection_timeout` / `connection_failed` | 检查被控端及自建服务的中继、防火墙配置 |
| `display_not_available` | 使用从 0 开始的有效显示器索引 |
| 鼠标无反应 | 确认已开启控制、点击在视频区域内，并允许终端鼠标报告 |
| `connection_lost` | 已建立的连接中断，检查网络后重新连接 |
| 终端无法显示像素画面 | 使用 ANSI，核对终端版本及图形协议支持 |

`doctor --json` 只检查本机环境，不验证远端可连接。通过 Xmake 运行后出现 `execv ... failed(1)` 仅表示程序以错误码退出，应查看它前面的具体错误。

错误信息会给出本次日志目录；默认位于 POSIX 的 `/tmp/crossdesk-cli-*` 或 Windows 的 `%TEMP%\crossdesk-cli-*`。反馈时提供系统、终端版本、渲染方式和报错，并检查附件中是否包含私人信息。

CLI 不提供音频、文件传输、剪贴板同步、IP 直连或可信设备认证。需要 Agent 截图与远程操作时使用 [MCP](../../mcp/README.md)。需要自行对接终端界面或脚本时，可查看 [桥接接口](bridge-protocol.md) 和 [自动化接口](automation-protocol.md)。
