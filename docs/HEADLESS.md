# Linux 无头运行

[返回 README](../README.md) · [English](HEADLESS_EN.md)

无显示器的 Linux 主机可复用已有 X11 桌面，也可通过 Xvfb 创建独立虚拟桌面。画面采集、键鼠、光标和剪贴板使用同一个显示会话。

## 保留原来的 Ubuntu 桌面和应用

正常打开 CrossDesk 即可，程序会自动发现并选择当前用户的已有桌面，保留 Dock、任务栏、壁纸和已打开应用。无需知道 `:0`、`:10` 等显示编号，也无需设置 `DISPLAY`：

```bash
# 安装包
crossdesk
# 源码构建
xmake r crossdesk
```

多个可用桌面同时存在时也会自动选择：优先启动环境中的桌面，其次是当前登录会话、系统标记的活动主桌面、其他活动桌面和仍在线的桌面。没有系统会话信息或优先级相同时，按稳定的显示顺序选择，避免每次启动切到不同桌面。失效会话与没有窗口管理器的空白 Xvfb 不会作为已有桌面选中。

复用已有桌面不依赖 Xvfb，也不会在退出 CrossDesk 时结束桌面或其他应用。若原会话已锁屏，远程会先看到系统锁屏界面，需要用该用户的系统密码解锁。

自动发现使用系统登录会话信息和当前用户的 X11 套接字，并验证窗口管理器确实可用。没有 systemd/logind 的发行版仍可自动发现并选择桌面。认证使用现有 `XAUTHORITY`（未设置时使用 Xlib 默认认证位置），必要时尝试当前用户的 GDM 认证文件。不会自动选择其他用户的桌面或登录欢迎界面。

`--headless` 可用于显式触发这套自动选择流程，例如忽略终端里失效的 Wayland 环境变量。`--headless-display` 仅供需要覆盖默认选择的高级使用者或调试使用：

```bash
crossdesk --headless-display :10  # 示例编号，以实际会话为准
```

此覆盖选项只连接指定的已有桌面；无法连接或没有运行窗口管理器时会报错，不会悄悄创建另一块屏幕。

**不要添加 `--headless-size` 或 `--headless-session` 来连接原桌面**：它们用于创建独立 Xvfb。原桌面的分辨率由原来的显示服务管理。

## 命令行控制台

无头模式下，终端只显示设备 ID、连接密码、在线状态和操作结果，常规日志写入文件。在 SSH / 无图形环境下普通启动会自动进入此模式；也可显式启用：

```bash
xmake r crossdesk --headless
```

程序运行后，在同一个终端输入命令并回车：

| 命令 | 作用 |
| --- | --- |
| `status` | 显示本机 ID、当前连接密码和在线状态 |
| `settings` | 打开设置菜单，输入编号或名称修改；`back` 返回 |
| `settings show` | 仅查看设置 |
| `settings set 名称 值` | 直接修改一个设置 |
| `password` | 提示输入新密码；6 位英文字母或数字，输入 `cancel` 取消 |
| `password Ab12Cd` | 直接提交新密码（示例，请自行选择密码） |
| `random` | 生成随机密码并提交修改 |
| `help` | 显示帮助 |
| `quit` | 关闭 CrossDesk；复用模式下不会关闭原桌面 |

设置菜单与 GUI 共用 `config.ini`，每项修改校验通过后立即保存，无需另执行保存命令。输入 `cancel` 放弃正在编辑的值。控制台状态、菜单、密码提示和帮助跟随语言设置，支持中文、英文和俄文；命令名称保持不变。

| 设置名称 | 可选值 / 格式 | 生效方式 |
| --- | --- | --- |
| `language` | `zh-CN` / `en-US` / `ru-RU` | 立即切换控制台和 GUI 语言，下次启动保留 |
| `codec` | `h264` / `av1` | 保存后重新连接 |
| `hardware` | `on` / `off` | 保存后重新连接；构建不支持时拒绝开启 |
| `turn` | `off` / `auto` / `udp` / `tcp` | 保存后重新连接；`udp` / `tcp` 表示强制中继 |
| `server_host` | 主机名或 IP，不含协议与路径 | 自托管未启用时保存备用，已启用时重连 |
| `server_port` | `1`–`65535` | 同上 |
| `self_hosted` | `on` / `off` | 先配置地址、端口，再开启；保存后重连 |
| `privacy` | `on` / `off` | 后续远程连接的自动隐私屏行为 |
| `file_path` | 已存在且可写的绝对目录；`default` 恢复默认 | 后续接收的文件；路径可含空格，无需额外引号 |
| `autostart` | `on` / `off` | 下次图形桌面登录时启动；不等于系统服务开机启动 |
| `daemon` | `on` / `off` | 普通模式下次启动生效；无头控制台仍保持前台 |

例如，在运行中的控制台输入：

```text
settings
1
en-US
back
settings set codec av1
settings set file_path /home/user/Received Files
```

存在远程会话（含正在连接的会话）或尚未完成的密码修改时，连接相关设置会被拒绝，避免中断会话。修改服务器地址和端口前，应先结束远程会话。切换自托管可能切换到该服务器对应的 ID 和密码，以重新连接后的 `status` 为准。画质和帧率由控制端管理，Linux 无头模式采用 X11 采集，这些不提供额外的全局修改入口。

修改密码需要连接到信令服务器。控制台会显示提交、确认和重新连接的结果；仅在服务器确认后采用新密码，并沿用 GUI 的凭据持久化与超时恢复流程。离线、密码格式错误或修改尚未完成时，会给出明确提示。密码修改会触发重新连接。

日志路径会在启动时显示，继续使用原有的 `crossdesk-YYYYMMDD-HHMMSS.log` 和 `minirtc-YYYYMMDD-HHMMSS.log`，文件命名和原有轮转规则不变，不额外创建无头模式日志文件。无头启动诊断写入 CrossDesk 日志；终端日志副本和第三方组件直接写向标准输出/标准错误的内容被静默处理。ID/密码状态输出直接送往终端，不经过日志系统。非交互运行（例如 systemd、重定向输出）默认不显示明文密码；标准输入关闭不会终止后台服务。

交互控制台在前台运行，内置 daemon 设置在此模式下不会将进程分离。当前已有旧版本进程时，需要退出旧实例后启动新版本才能使用控制台。

## 创建独立虚拟桌面

Debian/Ubuntu 安装包将 Xvfb、Xfce 和 D-Bus 会话工具声明为强依赖，使用 APT 安装时会自动补齐，即使指定 `--no-install-recommends` 也不会跳过。源码直接运行时，需在开发环境中准备同样的运行依赖；项目构建镜像和 CI 已包含这些组件。

原来没有图形会话，或者希望单独开一套桌面时，可启动独立 Xfce 会话。Ubuntu / Debian 示例（安装包用户无需重复安装依赖）：

```bash
sudo apt install xvfb xfce4 dbus
crossdesk --headless-size 1920x1080 --headless-session startxfce4
```

默认分辨率为 1920×1080，宽高必须是 320–8192 范围内的偶数。`--headless-session` 接收一个可执行程序名称或路径，通过独立的 `dbus-run-session` 启动。需要参数时，创建可执行脚本，在脚本中用 `exec` 启动桌面或窗口管理器。

仅做画面采集调试时，可以明确启动裸 Xvfb：

```bash
sudo apt install xvfb
crossdesk --headless-session none
```

裸 Xvfb 只提供屏幕，**不会生成 Dock、任务栏或原来的应用窗口**。只使用 `--headless-size` 而未指定桌面会话也属于这一情况，启动诊断日志会记录提示。此前“只看到 CrossDesk，其余一片黑”就是因为进入了这种独立裸显示。

## 自动选择与限制

- 普通图形启动继续使用现有 `DISPLAY` 或 Wayland 会话。X11 没有活动 RandR 输出或没有 RandR 扩展时，采集可用的根窗口。
- `--headless`，或没有可连接的 `DISPLAY` 且未设置 Wayland 环境时，会先自动选择已有 X11 桌面，再回退到独立 Xvfb；没有桌面环境时，回退的裸 Xvfb 会明确提示其不包含 Dock 和原应用。
- `--headless-display` 与 `--headless-size` / `--headless-session` 不能混用。
- `--no-headless` 禁用自动发现和回退；`--headless-help` 查看命令说明。
- 不会在显示会话之间迁移应用，也不会把运行中的会话自动切换到另一块屏幕。物理会话已经失效时，另建 Xvfb 无法恢复其应用。

## 后台运行

创建独立 Xvfb 时，在前台保留一个管理进程，监控 Xvfb、桌面会话和 CrossDesk。退出程序、发送 SIGINT / SIGTERM / SIGHUP，或任一受监控进程退出时，会清理该次会话的子进程与临时认证文件。显示编号由 Xvfb 自动分配，避免占用已有显示；TCP 监听关闭，X11 连接使用随机认证 cookie，认证文件仅当前用户可读。

启动诊断日志会记录 `DISPLAY` 和 `XAUTHORITY` 路径；在同一用户的终端设置这两个变量后，可以将其他应用启动到虚拟桌面。认证文件会随会话退出被删除。

需要开机后保持服务，可创建用户级 systemd 单元 `~/.config/systemd/user/crossdesk-headless.service`，并将 `ExecStart` 改为实际程序路径：

```ini
[Unit]
Description=CrossDesk virtual desktop

[Service]
Type=simple
ExecStart=/usr/bin/crossdesk --headless-size 1920x1080 --headless-session startxfce4
Restart=on-failure
RestartSec=5
KillMode=mixed
TimeoutStopSec=15

[Install]
WantedBy=default.target
```

```bash
systemctl --user daemon-reload
systemctl --user enable --now crossdesk-headless.service
journalctl --user -u crossdesk-headless.service -f
```

需要复用原桌面的服务应将 `ExecStart` 改为 `/usr/bin/crossdesk`（自动发现和选择；确保已有图形会话登录）。如需在用户未登录时运行用户服务，管理员需为该用户启用 linger：`sudo loginctl enable-linger 用户名`。独立 Xvfb 模式下内置 daemon 选项不会生效；systemd 负责重启整个虚拟桌面。停止该模式会结束虚拟桌面中的应用，请先保存工作。

## 运行限制

虚拟显示的 GUI 使用软件渲染，画面走 X11 → NV12 采集。视频编码仍遵循 CrossDesk 设置和本机编码器能力。声音依赖当前用户可用的音频服务；Xvfb 本身不提供虚拟声卡。此实现针对 Linux；Windows 无显示器支持见 [常见问题](FAQ.md)。
