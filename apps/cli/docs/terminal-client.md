# 终端客户端

## 构建与安装

要求 Git、Xmake >= 3.1.1、C++17 编译器、系统 zlib 头文件/链接库，以及 MiniRTC 包构建使用的系统工具。macOS SDK 已提供 zlib；Linux 需相应开发包（如 zlib1g-dev）；Windows 使用 Visual Studio C++ 构建工具，zlib 由 Xmake 获取并静态链接，运行库使用 MT。依赖需要可访问 Xmake 包仓库。Linux 使用 GCC/Clang；macOS 使用 Xcode 命令行工具。无需安装 Slint、SDL、X11、PulseAudio 或桌面采集 SDK。

```sh
./apps/cli/scripts/prepare-deps.sh
xmake f -y --CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y --USE_CUDA=n -m release
xmake build crossdesk-cli
xmake run crossdesk-cli doctor --json
```

`prepare-deps.sh` 固定 MiniRTC commit 并应用凭据日志脱敏补丁，可重复执行。构建会检查补丁，缺失时失败。不要直接升级子模块。仓库默认开启桌面构建、关闭 CLI。上述参数只构建 CLI；需要同时构建桌面和 CLI 时，将 `CROSSDESK_BUILD_DESKTOP` 设为 `y`。恢复默认桌面配置使用 `xmake f -y --CROSSDESK_BUILD_DESKTOP=y --CROSSDESK_BUILD_CLI=n`。所有命令均从仓库根目录执行。

构建产物在 `build/<platform>/<arch>/<mode>/crossdesk-cli`。可把对应目录加入 PATH 或复制可执行文件到自己的 bin 目录。Pi 需要直接启动该可执行文件，不能使用会输出构建日志的 `xmake run` 作为桥接程序。

macOS 的 MiniRTC 依赖仍链接 VideoToolbox/CoreMedia 和 GLib 声明的 AppKit 等系统框架；CLI 不创建 GUI、采集器或音频设备。硬件解码参数显式关闭。Linux 无需 DISPLAY/WAYLAND_DISPLAY；本轮尚未在 Linux 主机实际构建验证。

Windows 产物为 `build/windows/<arch>/release/crossdesk-cli.exe`；先在 Git Bash 中执行 `sh apps/cli/scripts/prepare-deps.sh`，然后可在开发者 PowerShell 中执行上述 Xmake 命令。Windows 使用 Console / ConPTY 的原生键盘、鼠标事件和 VT 输出，保存并恢复控制台模式与代码页；无需 WSL。Windows Terminal、现代 PowerShell / CMD 和 VS Code 的 ConPTY 终端属于目标运行环境，仍需实机验收。Git Bash 的独立 mintty 窗口没有原生控制台时，应从 Windows Terminal 的 Git Bash 配置启动。

预编译 npm 包的用户无需构建工具，见 [npm 分发](npm-distribution.md)。

## 连接与身份

```sh
crossdesk-cli view 123456789
crossdesk-cli view 123456789 --renderer ansi --fps 8 --quality high --zoom native
crossdesk-cli view 123456789 --renderer kitty --quality ultra --zoom native --display 0
crossdesk-cli view 123456789 --control
crossdesk-cli view 123456789 --server your-server.example --port 9099
```

设备 ID 必须是 9 位数字，以上数字仅示例。默认信令地址沿用当前 CrossDesk：`api.crossdesk.cn:9099`。密码从本地终端隐藏输入（POSIX `/dev/tty`、Windows `CONIN$` / `CONOUT$`），不提供密码命令行参数。CLI 不读取、不修改桌面配置。每次连接直接用 `cli` 请求服务端分配临时 `cli-xxxxxxxxx` ID（后缀为 9 位数字），注册、Join 和媒体使用同一个 Peer。CLI 仅作为控制端，不申请普通数字身份或 `C-` 身份，也不保存本机密码或重连凭据。每次启动或重新连接都会申请新的 ID；断开后服务端按 Web 临时身份的规则清理注册记录。

需先部署支持 `cli` 注册的新版 crossdesk-server；旧服务端会登录失败，本客户端不会回退为普通设备身份。桥接 `status.client_id` 返回服务端分配的 ID。

流程为创建主控 Peer、注册流 → 以 `cli` 注册 → SignalConnected 返回 `cli-xxxxxxxxx` → Join → Connected → 现有 HostInformation 握手 → 显示器选择、能力协商 → 视频。Init 返回 0 不是远端连接成功。连接总超时默认 30 秒；信令断开后结束本次会话，用户重新连接，不自动循环重试密码。

保留 MiniRTC WSS 证书链与主机名验证、TURN 临时凭据下发及 SRTP 协商。未提供跳过证书检查的参数。SRTP 是否实际协商成功通过状态统计呈现；沿用现有 MiniRTC 对旧端的兼容性，不宣称所有旧端必然启用 SRTP。自建证书问题应修复证书/系统信任链。

## ANSI、Kitty 与 SSH

ANSI 默认使用四象限字符：每个字符从 2×2 样本选择误差最小的两个颜色和象限形状，比原先上下半方块多一倍水平采样点；每个字符仍只有前景/背景两色。使用 `--ansi-detail half` 可回到上下半方块以兼容字体。缩小时采用面积加权滤波，避免最近邻采样漏掉细线。支持 TrueColor、256 色和基础 8 色，256 色选择实际颜色立方体或灰阶条中更接近的颜色。环境的 COLORTERM/TERM 决定颜色模式；非 UTF-8 locale 使用背景色空格。TERM=dumb、非 TTY 输出明确拒绝 view，不输出全屏转义序列。

画面按字符单元比例等比缩放，留黑边及状态栏，默认假定字符宽高比 0.5；TTY 提供像素尺寸时使用其比例。每次只输出变化行，合并连续颜色设置；缩放在完整输出边界清理旧画面。上限 240 列、100 画面行；默认预留两行及一列，避免自动换行。

ANSI 在完整桌面缩略图中仍无法保留普通字号的文字。先增加终端列数（缩小终端字体或扩大窗口），阅读局部时用 `--zoom native`。只读全景中点击目标位置可直接进入该区域的细节模式；按 `d` 则以当前中心进入。细节模式依据视口和字符宽高比选择源区域，避免为了完整装入桌面而把文字缩小；ANSI 视野较小，借助方向键平移阅读。`--zoom 1..32` 是相对整桌面的倍率；在细节模式中 `+/-` 是相对原始细节尺寸的倍率。画面缩放完全在本地进行，控制鼠标坐标映射回原始桌面的正确区域。

auto 模式发送 Kitty 查询与设备属性查询，等待最多 250ms；没有有效 Kitty 回应就用 ANSI。显式 kitty 模式探测失败时报错。tmux/screen 中 auto 使用 ANSI，显式 kitty 报不支持；不修改用户终端配置。

Kitty 通过终端字节流发送 zlib 压缩的内联 RGB/Base64（每块至多 4096 字符），不使用远端临时图片文件。图像固定 ID=31、placement=1，更新同一图像，退出删除。依据终端报告的像素尺寸生成画面（未知时按 10×20 像素字符估算），上限 1920×1080。保留黑边和宽高比；原始细节模式按像素视口裁取源区域，避免整桌面缩小。没有可靠终端像素尺寸时使用估算值，因此不承诺显示设备的物理像素一一对应。压缩后超出单次输出预算时逐级降低尺寸。高熵视频压缩率较低，同一输出预算下实际帧率可能低于目标。RGB 压缩无损，不代表上游视频编码无损。

```sh
ssh -t your-server
crossdesk-cli view 123456789 --renderer ansi --fps 8
```

用户本地终端负责显示 SSH 字节流。ANSI、Kitty over SSH 和 SSH+tmux 的实机互通尚未验证。无 GUI 指主控客户端；被控端仍需要可捕获的图形会话。

## 预览与背压

`--fps 1..30` 是本地输出目标。对端声明 PreviewSubscription 时，映射到合法 1/8/15 FPS（15 需要对应能力）；普通编码设置仍使用合法 30 FPS。分辨率上限使用协商到的 v2，视频预算只在 bandwidth 能力存在时发送。`--quality low|balanced|high|ultra` 对应如下请求：

| 档位 | 分辨率上限 | 质量枚举 | 视频预算 |
|---|---|---|---|
| low | 640×360 | 0 | 1Mbps |
| balanced | 1280×720 | 1 | 2.5Mbps |
| high（默认） | 1920×1080 | 2 | 6Mbps |
| ultra | 3840×2160 | 2 | 12Mbps |

这些是请求上限，实际视频尺寸与码率由被控端决定，ultra 不会把低分辨率源变成真实 4K。低带宽链路可显式选择 balanced/low。窗口缩小不会再把远端预览降到字符网格尺寸；选定清晰度用于后续局部放大。旧端继续基础观看并本地限帧缩放，不发送未声明的扩展。预览 request_id/ack/accepted 可从桥接状态检查；未确认或被拒绝不冒充成功，也不阻止基础视频。

媒体回调仅同步复制借用的 NV12 数据，校验尺寸、容量、native 描述符与 stride；不保留借用指针。上限单帧 16,777,216 像素、每边 8192、偶数尺寸。最多一个待转换帧，替换前先完成校验与复制，无效帧不会挤掉已有画面；播放器另保留最近转换的一帧用于静止桌面的本地放大/重绘，并在接收新帧时回收上一帧缓冲区供后续复制使用。只丢弃解码后图像。显示器切换/关闭提升 generation，旧帧失效。

转换与输出在拥有会话的事件循环，媒体回调独立。输出至多一个完整消息（2MiB），以 1MiB/s 令牌预算分块非阻塞写入；不能开始新画面时保留最新解码帧。ANSI 差分基准只在整帧输出完成后提交。Pi 桥接另要求消费帧 ACK，最多一帧等待 ACK，输入映射只在对应 ACK 后切换到新网格。状态栏优先在完整画面输出之间更新，避免大图持续刷新时状态饥饿；可写事件唤醒短写重试，仍遵守字节预算。控制事件至多 64 条，协议输入 64KiB/嵌套 32 层，输入粘贴缓冲 32KiB，文本单次最多 16KiB。

统计包括收到帧数、被替换帧数、非法帧、等待帧数、实际完整提交次数、输出字节数、运行秒数、最近本地排队时间、网络码率/RTT/SRTP。通过计数差/时间差可得到区间 FPS；view 结束日志另给平均提交 FPS。写入 TTY 完成不等于屏幕显示完成，本地排队时间不是端到端延迟。

## 输入与退出

默认只读、静音。`--control` 或只读时按 `c` 明确启用输入。`Ctrl+]` 是本地命令前缀：先按下并松开，再按下表中的命令键。状态栏即时显示当前模式与可用按键，单按前缀不会退出。

| 操作 | 只读模式 | 控制模式 |
|---|---|---|
| 开启／关闭远端控制 | `c` | `Ctrl+]`，再按 `c` |
| 退出 CLI | `q` 或 `Ctrl+C` | `Ctrl+]`，再按 `q` |
| 显示本地命令提示 | `?` 或 `Ctrl+]` | `Ctrl+]` |
| 放大／缩小 | `+` / `=` / `-` | `Ctrl+]`，再按对应键 |
| 原始细节／完整画面 | `d`（detail）/ `f`（fit），相邻字母键 | `Ctrl+]`，再按对应键 |
| 平移 | 方向键或 `i` 上、`j` 左、`k` 下、`l` 右 | `Ctrl+]`，再按对应键 |

进入本地命令状态时暂停远端输入，并释放已记录按键／鼠标；执行一个命令后恢复原模式，`c` 则切换模式。`Esc` 或 `Ctrl+C` 取消命令并恢复原模式；未知命令显示提示，不发送给远端。命令状态中的鼠标事件被忽略，bracketed paste 内容被丢弃并取消命令，不会触发快捷键。控制模式下普通 `c/q/?`、`Ctrl+T`、`Ctrl+C` 和单独 `Esc` 均传给远端；连续按两次 `Ctrl+]` 可向远端发送一次 `Ctrl+]`。目前不支持重绑定。

只读时，`d` 或在画面中单击进入该位置的原始细节模式；`+` / `=` 放大、`-` 缩小（1～32倍），滚轮以鼠标指向为中心调整倍率。方向键或 `i/j/k/l` 分别向上/左/下/右平移，`f` 恢复完整画面。黑边点击被忽略。这些按键和鼠标在控制模式下仍传给远端；使用本地命令前缀可调整一次视图，持续查看时可先切回只读。放大、平移和只读鼠标定位不会向远端发送操作，也适用于静止画面。

支持 SGR 鼠标移动、左右中键、拖动和滚轮；黑边不发送新点击，黑边处松开仍释放旧按钮。输入坐标映射到所选显示器，`--display` 是 HostInformation 列表的零起始索引，视频流为 Display1、Display2……

支持 UTF-8 文本与 bracketed paste（远端须声明 text_input），Enter、Tab、Backspace、Esc、方向、Home/End、Delete、PageUp/Down、F1–F4、Ctrl/Alt+英文字母。不支持的远端文本能力会提示，不把粘贴执行为本地 shell 命令。普通终端采用立即按下/释放的 tap 策略；不承诺持续按键、所有组合键、IME 或完整键盘布局。Pi 使用宿主 SDK 解码增强按键后转为这组基础输入。

失焦报告、关闭控制、切换显示器、退出和断开都尝试释放已记录输入；网络已中断时不能保证远端收到释放。正常退出在普通执行路径恢复终端模式、鼠标报告、光标、自动换行和备用屏幕。POSIX 同样处理 SIGINT/SIGTERM/SIGHUP；Windows 处理控制台 Ctrl+C / Ctrl+Break，恢复控制台模式与代码页。中止时最多等待两秒排空当前画面，随后发送取消/终止转义序列并恢复；输出连接彻底损坏或 SIGKILL 时无法保证恢复，可在 shell 执行 `reset`。

只读是本客户端行为限制，不是服务端强制的独立只读授权。没有文件/剪贴板/远端命令接收处理器。

## Pi 扩展

依据已下载的 `@earendil-works/pi-coding-agent` 与 `pi-tui` **1.4.0** SDK 实现和类型检查。旧的 `@mariozechner` 包未验证，缺少 ctx.mode 的宿主会降级。

```sh
export CROSSDESK_CLI="$PWD/build/macosx/arm64/release/crossdesk-cli"
pi -e ./apps/cli/plugins/pi/index.ts
```

在 Pi 输入 `/crossdesk` 或 `/crossdesk 123456789`。先输入 ID，再在自定义遮蔽组件输入密码。扩展使用独立 fd 3 写密码，随后关闭；不写配置、聊天、工具结果或模型上下文。查看组件与独立 CLI 使用相同的 `Ctrl+]` 命令前缀和上述模式按键，由 bridge 统一解析。插件默认请求 high 预览并显示 ANSI 象限字符行，支持同样的只读细节/放大/平移键，颜色深度依据终端环境回退，由 Pi 自身布局和刷新；不直接占用 TTY，不发送 Kitty 图形或自行打开鼠标报告。

需要全局发现时，把整个 `apps/cli/plugins/pi` 目录复制到 `~/.pi/agent/extensions/crossdesk/` 后重启 Pi 或执行 `/reload`。卸载时先关闭视图，删除该目录并 `/reload`；使用 `-e` 时从启动参数移除即可。无需修改包中的密码或设备配置。默认连接官方信令；自建信令当前通过独立 view 命令使用，插件尚无服务器设置界面。

仅在 `ctx.mode === "tui"`、hasUI 且进程双向 TTY 时打开组件。RPC/print/json 返回独立播放器提示，不声称内嵌视频。UI 关闭/会话退出先关闭 bridge stdin，超时 SIGTERM，再超时 SIGKILL。bridge 尚未就绪或已经停止时，组件可直接用 `q`、`Esc` 或 `Ctrl+C` 关闭。SDK 类型和模拟 UI 生命周期已验证，真实 Pi TUI 视觉刷新、增强按键与鼠标传递未实机验收。真实设备的 bridge 与独立 view 验证见 [验证记录](validation.md)。

官方依据：[扩展文档](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/docs/extensions.md)、[TUI 文档](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/docs/tui.md)、[Kitty 图形协议](https://sw.kovidgoyal.net/kitty/graphics-protocol/)。

## 故障排查与边界

- `incorrect_password`：重新启动并输入正确密码，不自动重复尝试。
- `device_not_found` / `device_offline_or_unavailable`：核对 ID、服务端和被控端在线状态。
- `signaling_login_failed` / `signaling_disconnected`：检查服务器、网络、代理、防火墙；确认服务端已支持 `cli` 注册。
- `cli_identity_not_supported`：服务端未返回有效的 `cli-` 加 9 位数字的身份，需升级或检查服务端。
- `tls_certificate_error`：检查证书链、主机名、机器时钟和系统 CA，不关闭验证。
- `connection_timeout` / `connection_failed`：检查 NAT/TURN 与被控端。TURN 凭据只取服务端下发值。
- `display_not_available`：换成远端声明的索引；单会话一次只显示一个屏幕。
- `media_initialization_failed`：检查编译依赖和本地媒体运行环境。
- 只读下按键不会发送到远端；远端未声明文本协议时文本输入不可用。
- 日志在进程创建的私有临时目录（POSIX `/tmp/crossdesk-cli-*`、Windows `%TEMP%\crossdesk-cli-*`）。view 的摘要在 client.log；底层诊断默认关闭且敏感 Join 日志已修补。目录不保存密码；使用完可删除。错误退出信息在恢复终端后显示。
- `doctor --json` 是环境报告，不会联网登录、主动探测 Kitty 或证明远端可连接。

第一轮无音频播放或音频订阅 API，SSH 服务器上的声音不会传回本地。无后台守护进程、共享媒体 Broker、IP 直连、MCP 或自动截图。其他宿主可以使用 [私有桥接](bridge-protocol.md)，没有可靠自定义 UI 时打开独立终端或给出播放器命令。

每个播放器/桥接进程只拥有自己的一个媒体会话，不向模型消息发送视频。
