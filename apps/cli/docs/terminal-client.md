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

Windows 仅支持 x64，产物为 `build/windows/x64/release/crossdesk-cli.exe`；先在 Git Bash 中执行 `sh apps/cli/scripts/prepare-deps.sh`，然后在开发者 PowerShell 中执行上述 Xmake 命令，配置时额外指定 `-p windows -a x64`。在 Git Bash 中构建时同样必须指定 `-p windows`，避免被识别为 MSYS 平台。Windows 使用 Console / ConPTY 的原生键盘、鼠标事件和 VT 输出，保存并恢复控制台模式与代码页；无需 WSL。Windows Terminal、现代 PowerShell / CMD 和 VS Code 的 ConPTY 终端属于目标运行环境，仍需实机验收。Git Bash 的独立 mintty 窗口没有原生控制台时，应从 Windows Terminal 的 Git Bash 配置启动。

预编译 npm 包的用户无需构建工具，见 [npm 分发](npm-distribution.md)。

## 连接与身份

```sh
crossdesk-cli 123456789
crossdesk-cli 123456789 --renderer ansi --fps 8 --quality high --zoom native
crossdesk-cli 123456789 --renderer kitty --quality ultra --zoom native --display 0
crossdesk-cli 123456789 --control
crossdesk-cli 123456789 --server your-server.example --port 9099
```

直接将设备 ID 作为第一个参数即可连接；`crossdesk-cli view <设备ID> [选项]` 保留为兼容别名，支持相同选项。设备 ID 必须是 9 位数字，保留前导零，以上数字仅示例。默认信令地址沿用当前 CrossDesk：`api.crossdesk.cn:9099`。密码从本地终端隐藏输入（POSIX `/dev/tty`、Windows `CONIN$` / `CONOUT$`），不提供密码命令行参数。CLI 不读取、不修改桌面配置。每次连接直接用 `cli` 请求服务端分配临时 `cli-xxxxxxxxx` ID（后缀为 9 位数字），注册、Join 和媒体使用同一个 Peer。CLI 仅作为控制端，不申请普通数字身份或 `C-` 身份，也不保存本机密码或重连凭据。每次启动或重新连接都会申请新的 ID；断开后服务端按 Web 临时身份的规则清理注册记录。

需先部署支持 `cli` 注册的新版 crossdesk-server；旧服务端会登录失败，本客户端不会回退为普通设备身份。桥接 `status.client_id` 返回服务端分配的 ID。

流程为创建主控 Peer、注册流 → 以 `cli` 注册 → SignalConnected 返回 `cli-xxxxxxxxx` → Join → Connected → 现有 HostInformation 握手 → 显示器选择、能力协商 → 视频。Init 返回 0 不是远端连接成功。连接总超时默认 30 秒；信令断开后结束本次会话，用户重新连接，不自动循环重试密码。

交互界面区分 `Connecting to server`（连接信令）、`Connecting to device`（连接设备）、`Connected, waiting for first frame`（已连接，等待首帧）和 `Displaying`（正在显示）。首帧前显示居中的等待提示及秒数；连接阶段从发起连接计时，首帧等待阶段从连接成功重新计时。首帧完整写入终端后才切换到 `Displaying` 并清除等待提示。已连接但暂时没有画面不会因此判为连接失败，也不代表中继未启用。状态栏右侧为接收视频码率，收到网络统计后显示 `P2P` 或 `RELAY`，码率按 bps、kbps、Mbps 显示；初始码率为零或很低时仍继续等待视频。

保留 MiniRTC WSS 证书链与主机名验证、TURN 临时凭据下发及 SRTP 协商。未提供跳过证书检查的参数。SRTP 是否实际协商成功通过状态统计呈现；沿用现有 MiniRTC 对旧端的兼容性，不宣称所有旧端必然启用 SRTP。自建证书问题应修复证书/系统信任链。

## ANSI、Kitty 与 SSH

ANSI 默认使用四象限字符：每个字符从 2×2 样本选择误差最小的两个颜色和象限形状，比原先上下半方块多一倍水平采样点；每个字符仍只有前景/背景两色。使用 `--ansi-detail half` 可回到上下半方块以兼容字体。缩小时采用面积加权滤波，避免最近邻采样漏掉细线。支持 TrueColor、256 色和基础 8 色，256 色选择实际颜色立方体或灰阶条中更接近的颜色。环境的 COLORTERM/TERM 决定颜色模式；非 UTF-8 locale 使用背景色空格。TERM=dumb、非 TTY 输出明确拒绝交互连接，不输出全屏转义序列。

四象限模式将每个 RGB 通道变化均不超过 6/255 的低对比单元合并为纯背景色，减少平坦区域的字形接缝。其余单元在独立拟合后参考上下左右邻格的量化误差调整选色，保留源图的真实渐变，并在高对比边缘减弱约束；调整设有失真上限，超过低对比阈值且能精确表示的文字细线和双色边缘保持原样。这些处理自动应用于独立 CLI 和 Pi／bridge 的四象限画面，实际效果仍受终端字体和行距影响。

画面按字符单元比例等比缩放，留黑边及状态栏。交互连接通过终端字节流查询字符尺寸，支持通过 SSH 返回的回复：优先使用 `CSI 16 t` 的字符像素尺寸，否则组合 `CSI 14 t` 的完整文本区像素尺寸和 `CSI 18 t` 的完整行列数计算，兼容 GNOME/VTE 的文本区报告。计算后再扣除状态栏及画面行列上限，不把预留行当作图像高度。窗口改变时重新查询，每两秒刷新一次以覆盖字体／行距变化；回复到达后会重绘静止画面和更新鼠标映射。查询不阻塞连接；没有有效回复时使用系统提供的像素尺寸，再缺失时回退为宽高比 0.5。曾收到有效回复时保留最近有效尺寸，直到下一次有效回复。字符栅格的整数舍入仍可能产生小幅比例误差。格式依据 [XTerm 窗口尺寸报告](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html) 和 [GNOME VTE 实现](https://github.com/GNOME/vte/blob/master/src/vteseq.cc)。

Windows 的可见传统控制台优先读取当前字体尺寸；Windows Terminal／ConPTY 使用显示终端的尺寸回复，不采用隐藏控制台宿主的字体值。不同终端版本可能不支持查询，或返回虚拟图形尺寸而非实际字体尺寸，此时需要手动校准。

默认 `--cell-aspect auto` 自动确定比例。如果终端屏蔽查询或需要手动校准，可设置 `--cell-aspect 0.45`，数值表示单个字符的**宽／高**，允许 0.2～2；例如字符为 9×20 像素时使用 0.45。画面被压窄时调小此值，被拉宽时调大。此参数优先于自动检测，同时用于全景、细节和鼠标坐标映射。

独立 CLI 每次只输出变化行中首尾变化单元之间的区间，光标或局部视频更新不再重写整行；一帧内复用颜色状态、按实际色板索引合并颜色设置，并省略纯背景空格的前景色指令。Pi／bridge 仍发送可独立绘制的完整字符行，同时使用上述颜色编码优化。四象限初始拟合使用整数子集求和计算同等精度的颜色和误差，边界连续性拟合复用加权样本，减少重复运算。

缩放在完整输出边界清理旧画面。上限 240 列、100 画面行；默认顶部预留一行状态栏、底部预留两行快捷键，另预留一列避免自动换行。顶部状态栏用反色区分，左侧为控制模式和连接进度，右侧为画面参数及网络信息；底部两行分别为会话和视图快捷键。视频和等待提示从顶部状态栏下方开始，鼠标坐标按视频区域映射，点击状态栏不会传给远端。按键分组使用统一列宽和左右留白，窄窗口自动减少列数、合并按键组并省略次要参数，累计帧数和输出字节数保留在日志中。控制模式下仅提示本地命令入口及后续操作，进入本地命令模式后才显示平移和缩放按键；终端高度不足时优先收起底部提示行。

快捷键行使用 `KEYS |`、`VIEW |` 等固定宽度标题，标题和按键组之间用竖线分隔；键名用 `<c>`、`<+/->` 等标签标识。宽窗口中的分组分隔线按列对齐，窄窗口保留标题和完整按键标签，按可用宽度合并或减少分组。

ANSI 在完整桌面缩略图中仍无法保留普通字号的文字。先增加终端列数（缩小终端字体或扩大窗口），阅读局部时用 `--zoom native`。只读全景中点击目标位置可直接进入该区域的细节模式；按 `d` 则以当前中心进入。细节模式依据视口和字符宽高比选择源区域，避免为了完整装入桌面而把文字缩小；ANSI 视野较小，借助方向键平移阅读。`--zoom 1..32` 是相对整桌面的倍率；在细节模式中 `+/-` 是相对原始细节尺寸的倍率。画面缩放完全在本地进行，控制鼠标坐标映射回原始桌面的正确区域。

auto 模式发送 Kitty 查询与设备属性查询，等待最多 250ms；没有有效 Kitty 回应就用 ANSI。显式 kitty 模式探测失败时报错。tmux/screen 中 auto 使用 ANSI，显式 kitty 报不支持；不修改用户终端配置。

本地 macOS/Linux 的 Kitty 模式会另用一个 1×1 像素的共享内存对象探测 `t=s`，最多等待 250ms；探测成功后，将未压缩 RGB 写入权限为 0600 的 POSIX 共享内存，仅通过终端字节流发送名字和显示命令。每次使用唯一名字，最多保留一个待确认的共享缓冲区（1920×1080×3 字节）。收到上传成功确认后才显示新图并释放缓冲区；上传报错、确认超过 500ms 或本地分配失败时，本次会话退回内联传输，继续保留旧画面。终端和 CLI 都会清理共享对象，退出时也会释放未确认的对象。SSH 中不尝试共享内存；Windows 暂用内联传输。

内联 Kitty 通过终端字节流发送 zlib 压缩的 RGB/Base64（每块至多 4096 字符），不使用远端临时图片文件。两条路径都交替使用图像 ID=31/32：以 `a=t` 上传到未显示的 ID，传完后以 `a=p` 创建 placement=1，再删除旧图。仅在整帧显示命令输出完成后交替 ID，上传和短写期间保留旧画面，退出时清理两个 ID。这样避免重传同一 ID 时终端先移除旧图造成的帧间黑屏。

依据终端报告的像素尺寸生成画面（未知时按 10×20 像素字符估算），上限 1920×1080。保留面积滤波、黑边、宽高比和裁剪坐标；原始细节模式按像素视口裁取源区域。共享内存路径使用可复用水平行计算的整数面积权重，避免逐像素重复浮点运算，也不再因 Base64 字节预算额外降采样。没有可靠终端像素尺寸时使用估算值，因此不承诺显示设备的物理像素一一对应。内联压缩结果超出单次输出预算时仍逐级降低尺寸；高熵视频的实际帧率可能低于目标。RGB 传输和压缩不丢失像素，不代表面积采样或上游视频编码无损。

```sh
ssh -t your-server
crossdesk-cli 123456789 --renderer ansi --fps 8
```

用户本地终端负责显示 SSH 字节流。ANSI、Kitty over SSH 和 SSH+tmux 的实机互通尚未验证。无 GUI 指主控客户端；被控端仍需要可捕获的图形会话。

## 预览与背压

`--fps 1..30` 是本地输出目标。交互 CLI 的 ANSI、内联 Kitty 和共享内存 Kitty 默认均为 30 FPS；共享内存失败回退时保持该目标。bridge 仍按宿主协议默认使用 8 FPS。

未显式指定 `--fps` 时，远端预览从 30 FPS 开始，根据完整一帧从转换开始到输出完成的耗时自动选择 8／15／30 FPS 请求；共享内存的上传确认等待也计入，等待源帧的空闲时间不计入。使用平滑估计、不同升降档阈值和至少 8 个新帧样本，持续变慢 2 秒才降档、持续恢复 4 秒才升档，保留供帧余量。15 FPS 仅在对端声明支持时使用；预览未获接受时不追加自动调节。视口变化、显示器切换和共享内存回退会重置耗时估计。显式 `--fps` 固定本地目标和对应的远端预览请求，关闭自动调节；例如 `--fps 30` 可用于与自动策略对比。

本地调度只在有新帧或需要重绘时消耗刷新时隙；短暂迟到保持原刷新节奏，长时间阻塞后跳过旧时隙，不连续补画过时帧。目标帧率不代表终端实际显示帧率。

对端声明 PreviewSubscription 时，预览请求不超过 15 映射到合法 1/8/15 FPS（15 需要对应能力）；超过 15 则请求项目已有的交互模式：协议 `fps=60` 是模式标记，编码设置 `frame_rate=30` 才是帧率。分辨率上限使用协商到的 v2，视频预算只在 bandwidth 能力存在时发送。`--quality low|balanced|high|ultra` 对应如下请求：

| 档位 | 分辨率上限 | 质量枚举 | 视频预算 |
|---|---|---|---|
| low | 640×360 | 0 | 1Mbps |
| balanced | 1280×720 | 1 | 2.5Mbps |
| high（默认） | 1920×1080 | 2 | 6Mbps |
| ultra | 3840×2160 | 2 | 12Mbps |

这些是请求上限，实际视频尺寸与码率由被控端决定，ultra 不会把低分辨率源变成真实 4K。低带宽链路可显式选择 balanced/low。窗口缩小不会再把远端预览降到字符网格尺寸；选定清晰度用于后续局部放大。旧端继续基础观看并本地限帧缩放，不发送未声明的扩展。预览 request_id/ack/accepted 可从桥接状态检查；未确认或被拒绝不冒充成功，也不阻止基础视频。

媒体回调仅同步复制借用的 NV12 数据，校验尺寸、容量、native 描述符与 stride；不保留借用指针。上限单帧 16,777,216 像素、每边 8192、偶数尺寸。最多一个待转换帧，替换前先完成校验与复制，无效帧不会挤掉已有画面；播放器另保留最近转换的一帧用于静止桌面的本地放大/重绘，并在接收新帧时回收上一帧缓冲区供后续复制使用。只丢弃解码后图像。显示器切换/关闭提升 generation，旧帧失效。

转换与输出在拥有会话的事件循环，媒体回调独立。输出至多一个完整消息（2MiB），按令牌预算分块非阻塞写入，突发额度上限仍为 1MiB。本地 Kitty 使用 8MiB/s；存在 `SSH_CONNECTION`、`SSH_CLIENT` 或 `SSH_TTY` 时，Kitty 保持 1MiB/s，ANSI 和 Pi 桥接也保持 1MiB/s。不能开始新画面时保留最新解码帧。ANSI 差分基准只在整帧输出完成后提交。Pi 桥接另要求消费帧 ACK，最多一帧等待 ACK，输入映射只在对应 ACK 后切换到新网格。状态栏优先在完整画面输出之间更新，避免大图持续刷新时状态饥饿；可写事件唤醒短写重试，仍遵守字节预算。控制事件至多 64 条，协议输入 64KiB/嵌套 32 层，输入粘贴缓冲 32KiB，文本单次最多 16KiB。

测量像素渲染帧率时使用 release 构建；debug 的 `-O0` 面积采样开销明显更高。共享内存中的像素不经过终端字节预算，但仍受转换和终端消费速度限制；8MiB/s 是内联路径的输出上限，不是帧率保证。解码、像素转换、压缩、终端消费速度和帧大小都会影响结果。退出时，未完成画面最多继续写入 2s；取消图像传输及恢复屏幕的短写最多另重试 500ms。POSIX 立即恢复 termios 并清理输入，不等待输出排空；如果终端始终不消费输出，屏幕清理只能尽力发送。

统计包括收到帧数、被替换帧数、非法帧、等待帧数、实际完整提交次数、输出字节数、运行秒数、最近本地排队时间、网络码率/RTT/SRTP。通过计数差/时间差可得到区间 FPS；交互连接结束日志另给平均提交 FPS，以及实际 `renderer` 和 `output_bytes_per_second` 预算。`kitty_transport=shared_memory/inline/none` 标明生效路径，`target_fps` 是本地目标，`preview_fps` 是最近请求的远端预览目标（并非已生效或实测帧率），`adaptive_preview` 表示是否开启自动调节，`preview_adjustments` 记录切档次数；每次切档另记录 `preview_fps` 事件。`frame_service_ms` 是最近完整帧处理与输出耗时的平滑估计，`frame_processing_ms` 是平均本地布局／转换／传输准备耗时，`shared_ack_ms` 是平均上传命令写完到解析成功确认的耗时，`shared_fallbacks` 记录运行时回退次数。`client.log` 摘要的 `startup_ms` 记录从发起连接到连接成功（`connected`）、首个取出用于显示的视频帧入队（`first_frame`）、首帧显示命令完整写入终端（`first_output`）的毫秒数；未到达的阶段为 `null`。写入 TTY 完成不等于屏幕显示完成，本地排队时间不是端到端延迟。

## 输入与退出

默认只读、静音。`--control` 或只读时按 `c` 明确启用输入。`Ctrl+]` 进入持续的本地命令模式：先按下并松开，再按下表中的命令键，可以连续执行多个命令。状态栏即时显示当前模式与可用按键。

macOS 自带 Terminal 还需要在当前窗口勾选“显示 → 允许鼠标报告”（View → Allow Mouse Reporting）。CLI 会启用鼠标报告，但无法覆盖 Terminal 关闭此选项的设置；未勾选时，点击、拖动和滚轮事件不会传给 CLI。参见 [Apple 官方说明](https://support.apple.com/zh-cn/guide/terminal/trmlc69728a5/mac)。

| 操作 | 只读模式 | 控制模式 |
|---|---|---|
| 开启／关闭远端控制 | `c` | `Ctrl+]`，再按 `c` |
| 退出 CLI | `q` 或 `Ctrl+C` | `Ctrl+]`，再按 `q` |
| 进入本地命令模式 | `?` 或 `Ctrl+]` | `Ctrl+]` |
| 放大／缩小 | `+` / `=` / `-` | `Ctrl+]`，再按对应键 |
| 原始细节／完整画面 | `d`（detail）/ `f`（fit），相邻字母键 | `Ctrl+]`，再按对应键 |
| 平移 | 方向键或 `i` 上、`j` 左、`k` 下、`l` 右 | `Ctrl+]`，再按对应键 |

进入本地命令模式时暂停远端输入，并释放已记录按键／鼠标。执行缩放、平移、`c` 或未知命令后均保持此模式；`c` 切换手动返回后是否启用远端控制，状态栏显示 `CONTROL PAUSED` 或 `READ ONLY`。`Esc` 或 `Ctrl+C` 手动返回所选模式；`q` 退出 CLI。再次按 `Ctrl+]` 也会返回，并在所选模式为控制时向远端发送一次 `Ctrl+]`。命令模式中的鼠标事件被忽略，bracketed paste 内容被丢弃，均不会退出模式或触发快捷键。窗口失焦或输入缓冲区溢出时继续保持本地命令模式，但将返回模式设为只读。控制模式下普通 `c/q/?`、`Ctrl+T`、`Ctrl+C` 和单独 `Esc` 均传给远端。目前不支持重绑定。

只读时，`d` 或在画面中单击进入该位置的原始细节模式；`+` / `=` 放大、`-` 缩小（1～32倍），滚轮以鼠标指向为中心调整倍率。方向键或 `i/j/k/l` 分别向上/左/下/右平移，`f` 恢复完整画面。黑边点击被忽略。这些按键和鼠标在控制模式下仍传给远端；进入本地命令模式后可连续调整视图，直至手动返回。放大、平移和只读鼠标定位不会向远端发送操作，也适用于静止画面。

支持 SGR 和传统 `CSI M` 鼠标报告，包括移动、左右中键、拖动和滚轮；完整缓冲跨读取分片的传统报告，坐标字节不会作为按键执行。启动时依次启用普通、拖动和全移动报告，兼容只支持部分跟踪模式的终端；退出时关闭这些模式。传统报告的坐标上限为 223，较大窗口应使用支持 SGR 的终端。协议格式依据 [XTerm 鼠标协议](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-Mouse-Tracking)。

点击和滚轮先发送对应位置的移动事件，不依赖终端提前上报移动；黑边不发送新点击，黑边处松开仍释放旧按钮。传统报告无法区分松开的按钮，因此释放本客户端记录的全部已按下鼠标按钮。输入坐标映射到所选显示器，`--display` 是 HostInformation 列表的零起始索引，视频流为 Display1、Display2……

控制模式显示画面后，状态栏的 `Mouse RX`（窄窗口为 `M:`）显示累计收到的有效鼠标报告数，即使画面中看不清远端指针也能检查输入。若点击或移动时计数一直为 0，检查本地终端的鼠标报告设置及 SSH 是否分配 PTY（`ssh -t`）；若计数增长但远端仍无响应，退出后查看以下日志统计。窗口失焦会退回只读，重新聚焦后需再按 `c` 开启控制。

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

Pi 扩展读取宿主的字符尺寸，并通过宿主终端写入／输入监听接口查询字符尺寸或完整文本区尺寸，每两秒及窗口变化时刷新；尺寸回复不会传给远端。得到的宽高比通过 bridge 的 `viewport.cell_aspect` 传入，比例变化时重绘静止画面，并丢弃仍在途的旧比例帧。自动检测不可用或不准确时，可在启动 Pi 前设置环境变量 `CROSSDESK_CELL_ASPECT=0.45`（宽／高，允许 0.2～2；`auto` 恢复自动检测）。视图关闭或 bridge 停止时移除查询定时器和输入监听。

需要全局发现时，把整个 `apps/cli/plugins/pi` 目录复制到 `~/.pi/agent/extensions/crossdesk/` 后重启 Pi 或执行 `/reload`。卸载时先关闭视图，删除该目录并 `/reload`；使用 `-e` 时从启动参数移除即可。无需修改包中的密码或设备配置。默认连接官方信令；自建信令当前通过独立 CLI 连接命令使用，插件尚无服务器设置界面。

仅在 `ctx.mode === "tui"`、hasUI 且进程双向 TTY 时打开组件。RPC/print/json 返回独立播放器提示，不声称内嵌视频。UI 关闭/会话退出先关闭 bridge stdin，超时 SIGTERM，再超时 SIGKILL。bridge 尚未就绪或已经停止时，组件可直接用 `q`、`Esc` 或 `Ctrl+C` 关闭。SDK 类型和模拟 UI 生命周期已验证，真实 Pi TUI 视觉刷新、增强按键与鼠标传递未实机验收。真实设备的 bridge 与独立 CLI 连接验证见 [验证记录](validation.md)。

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
- 日志在进程创建的私有临时目录（POSIX `/tmp/crossdesk-cli-*`、Windows `%TEMP%\crossdesk-cli-*`）。交互连接的摘要在 client.log，包含收到的终端鼠标报告数 `mouse_reports`、其中的传统报告数 `mouse_legacy_reports`、成功交给传输层的鼠标事件数 `mouse_forwarded`、发送失败数 `mouse_send_failures`。一次点击报告可能生成定位和按键两条事件，所以发送数可能大于报告数；发送成功不表示被控端已执行。transport.log 只记录白名单中的 ICE/DTLS 状态及本地控制开关，不记录密码、SDP 或原始信令。使用完可删除。错误退出信息在恢复终端后显示，并给出本次日志目录。
- `connection_failed` 表示连接建立失败；`connection_lost` 表示连接成功后传输失效。xmake 随后的 `execv ... failed(1)` 表示程序以错误码 1 退出，本身不代表进程崩溃。遇到控制失效时，对照 transport.log 中的控制开关和连接状态；`mouse_reports` 为 0 表示终端没有向 CLI 提供可识别的鼠标事件。报告数增长但 `mouse_forwarded` 为 0 时，检查是否仍为只读／本地命令模式、是否已有画面，以及点击是否落在视频区域；`mouse_send_failures` 增长时检查连接状态。
- 如果整个 macOS Terminal 窗口退出且 CLI 记录 `output closed`，还应检查系统 DiagnosticReports 中的 Terminal 报告。已观察到 Terminal 2.15 在创建标签页并调整窗口尺寸时发生 `TTLogicalScreen expandUnicharOffsetCacheToLine:` 行缓存越界；这与 CLI 的 `connection_failed` 不同。临时可在独立窗口运行，避开该标签页操作，不能据此认为 Terminal 缺陷已修复。
- `doctor --json` 是环境报告，不会联网登录、主动探测 Kitty 或证明远端可连接。 `input_shortcuts=local_commands_v2` 表示支持本文的持续本地命令模式；启动日志同时记录实际执行路径，以便区分不同工程的构建产物。
- 画面比例异常时查看 `client.log` 摘要的 `terminal_geometry`：`cell_aspect` 为实际采用的字符宽高比，`reported` 表示收到有效的终端尺寸报告，`manual_aspect` 表示使用了手动覆盖。`doctor` 中的比例和像素尺寸只反映系统 TTY 信息，不主动查询终端，可能与交互连接自动检测后的值不同。

第一轮无音频播放或音频订阅 API，SSH 服务器上的声音不会传回本地。无后台守护进程、共享媒体 Broker、IP 直连、MCP 或自动截图。其他宿主可以使用 [私有桥接](bridge-protocol.md)，没有可靠自定义 UI 时打开独立终端或给出播放器命令。

每个播放器/桥接进程只拥有自己的一个媒体会话，不向模型消息发送视频。
