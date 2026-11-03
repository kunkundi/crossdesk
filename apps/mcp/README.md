# CrossDesk MCP

让本机 Agent 通过 MCP 观察并操作远端 CrossDesk，同时为用户提供浏览器、终端或 MCP Apps 监看面板。控制端不需要安装 CrossDesk 桌面客户端；被控端仍需运行兼容的 CrossDesk，并连接支持 `cli` 注册的信令服务器。

此目录实现 MCP 包 `@chunkdash/crossdesk-mcp` 0.1.0，绑定原生 CLI 包 0.2.0。**源码实现和本地打包不代表这些版本已经发布到 npm。** 发布后的安装示例需要以实际可用版本为准。

## 从源码运行

仓库根目录构建原生程序（已有工作区可保留原有桌面构建选项）：

```sh
sh apps/cli/scripts/prepare-deps.sh
xmake f -y --CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y --USE_CUDA=n -m release
xmake build crossdesk-cli
cd apps/mcp
npm ci
npm run build
```

Node.js 要求 22 或以上。在 Agent 宿主中配置本地 stdio 进程，例如 macOS arm64 源码构建：

```json
{
  "mcpServers": {
    "crossdesk": {
      "command": "node",
      "args": [
        "/absolute/path/crossdesk/apps/mcp/dist/main.js",
        "serve", "--stdio", "--panel=auto",
        "--native-binary", "/absolute/path/crossdesk/build/macosx/arm64/release/crossdesk-cli"
      ]
    }
  }
}
```

Windows 和 Linux 使用各自构建产物路径；`--native-binary` 必须指向可信原生可执行程序，不接受 shell 命令。也可以通过 `CROSSDESK_NATIVE_BINARY` 显式指定开发产物。未指定时只从包内的固定版本平台依赖解析，不搜索 PATH。服务启动不连接远端、不打开浏览器；连接时才检查 `doctor` 协议并启动原生子进程。

发布包的配置形式：

```json
{
  "mcpServers": {
    "crossdesk": {
      "command": "npx",
      "args": ["-y", "@chunkdash/crossdesk-mcp@<已发布版本>", "serve", "--stdio", "--panel=auto"]
    }
  }
}
```

平台依赖覆盖 macOS arm64/x64、glibc Linux arm64/x64 和 Windows x64。安装时保留 optional dependencies；不需要另装全局 CLI。原生运行要求沿用 [CLI 分发文档](../cli/docs/npm-distribution.md)。`npm ci` 仅安装此源码目录的开发依赖，开发运行需使用 `--native-binary`；生成的发布包才包含固定平台依赖。

## 用户连接流程

1. 对 Agent 说“打开 CrossDesk”。`crossdesk_open` 创建 `view_id`，返回面板选择和等待用户状态。
2. 在面板选择已保存设备，或输入 9 位设备 ID；密码填写在面板里。可选择仅查看或允许 Agent 控制。
3. “保存设备”“记住密码”“允许以后自动连接”分别控制设备簿、系统凭据库、持久权限。后两项要求保存设备，默认均关闭。保存密码不自动授予控制权限。
4. 有待执行的远程任务时，Agent 在打开面板后立即调用 `crossdesk_get_session({view_id, wait_ms: 25000})` 等待连接。连接就绪后，工具直接返回会话 ID 和权限状态，Agent 继续任务；用户无需回到对话发送连接确认，也无需点击继续按钮。浏览器和 Apps 面板使用同一流程。
5. Agent 获取独立截图，用截图的 `observation_id` 和图像坐标提交动作。面板预览不消耗模型截图序列。

`crossdesk_open` 立即返回，让 Apps 宿主有机会渲染面板；等待连接在后续 `crossdesk_get_session` 调用中完成。按 `view_id` 查询默认最多等待 25 秒，`wait_ms: 0` 则立即查询；未就绪时返回 `wait_timed_out: true`，Agent 在原任务仍待执行时继续等待。按 `session_id` 查询仍立即返回。取消等待只取消本次查询，不断开用户正在建立或使用的连接。用户主动断开后返回 `closed`，Agent 停止等待，不自动重连。

面板发起的连接失败后，按 `view_id` 等待也会返回 `failed` 和错误码，避免 Agent 一直停留在等待用户状态。断开正在建立的连接会取消整个连接尝试。多个面板和 Agent 同时查询状态时共用一次原生查询，单次查询取消或暂时繁忙不会断开会话。`crossdesk_connect` 在连接成功但面板初始化失败时仍返回有效的 `session_id`，并附加 `panel_error`。

自动继续依赖宿主保持当前 Agent 任务及工具调用流程。它不通过网页伪造用户消息，也不承诺从已经结束的对话中主动唤醒 Agent；此类唤醒由宿主负责。

人工暂停由原生层锁定，模型不能通过 `set_control(true)` 恢复；只有当前授权面板的恢复按钮可以恢复。仅查看连接不能升级为控制连接，需重新连接并授权。关闭/隐藏面板仅停止预览，远程会话保留；断开按钮或 `crossdesk_disconnect` 才结束会话。

同一 MCP 进程默认只保留一个活动远程会话。相同配置目录中的进程用设备锁避免同时连接同一目标；它不协调另一个系统用户、另一份配置目录或独立桌面客户端。

## 面板策略

`--panel` 优先于设备配置中的 `panel`，默认 `auto`。

| 模式 | 行为 |
| --- | --- |
| `auto` | 有本机 GUI 时打开浏览器；无 GUI 且检测到终端环境时提供 CLI 画面入口；两者均无则使用 `off` |
| `apps` | 只使用 MCP Apps；能力不匹配返回 `PANEL_UNSUPPORTED` |
| `browser` | 只使用本地浏览器，不附加 Apps 工具资源元数据 |
| `terminal` | 提供独立终端 `view` 命令，共用当前 MCP 会话，不启动网页服务 |
| `off` | 不启动网页服务，保留截图和操作工具，凭据通过独立 `setup` 配置 |

`auto` 不再优先选择 MCP Apps；需要宿主内嵌面板时显式使用 `--panel=apps`。Apps 使用 `extensions["io.modelcontextprotocol/ui"].mimeTypes` 中的 `text/html;profile=mcp-app` 判断能力。旧协议读取 initialize 能力；2026-07-28 协议读取当前请求的能力，不按宿主产品名猜测支持情况。Apps 的卡片、侧栏或窗口形式由宿主决定。

Linux 根据 `DISPLAY` / `WAYLAND_DISPLAY` 判断 GUI；macOS 检查当前用户的桌面登录会话，SSH 会话不自动打开远端 Mac 的浏览器；Windows 排除 SSH 和 Services 会话。终端判断包含 TTY、`SSH_TTY` 和非 `dumb` 的 `TERM`，因为 MCP 的 stdin/stdout 通常是管道。环境变量被宿主清除或识别不适用时，可显式设置 `--panel=terminal` 或 `--panel=browser`。

终端模式的 `crossdesk_open` / `crossdesk_connect` 返回 `terminal_command`。用户在同一主机的另一个交互终端执行（源码运行时将 `crossdesk-mcp` 替换为 `node /absolute/path/apps/mcp/dist/main.js`）：

```sh
crossdesk-mcp view --view <view_id> --config-dir <相同的配置目录>
```

该命令提供设备选择、隐藏密码输入和本次控制授权；已建立会话则直接显示画面。终端通过受限配置目录中的运行描述符和认证本地 IPC 复用 MCP 的原生连接，不启动第二个远程连接，也不占用 Agent 的终端或 MCP stdio。Agent 应展示命令供用户执行，再通过 `crossdesk_get_session` 等待；不能在工具输出捕获通道中运行 `view`。无交互终端时 `view` 返回 `VIEW_REQUIRES_TERMINAL`。

终端画面采用原生 CLI 的 ANSI 四分块渲染器，目标 8 FPS，最多 160 列 × 60 行，根据终端环境使用 8/256/真彩色。默认字符宽高比 0.5，可用 `--cell-aspect`（0.2–2）调整。此入口暂不使用 Kitty/Sixel。`p` 暂停 Agent、`r` 恢复已授权控制、`d` 断开、`q` / Ctrl+C 仅关闭画面并恢复终端；关闭画面后会话保留。暂停仍由原生层锁定，Agent 不能自行恢复。

浏览器/Apps 与终端均绘制 Agent 最后成功发送的鼠标位置，支持移动、点击、滚动和拖拽；坐标来自原生层对截图缩放及裁剪的映射。暂停或关闭控制时隐藏，切换显示器或图像尺寸变化时清除。该指针是 Agent 的操作位置提示，不代表远端输入执行 ACK，也不跟踪远端用户自己的鼠标。它不写入模型截图或源视频。

Apps 资源为 `ui://crossdesk/viewer.html`，工具只在允许使用 Apps 且当前请求支持时暴露 UI 元数据和面板专用接口。页面回报 `crossdesk_ui_ready` 后才标记就绪；10 秒未回报显示 `not_confirmed`，不自动弹第二个浏览器。用户明确要求回退时调用 `crossdesk_open_browser`；该工具不附带 Apps 元数据，`apps`/`off` 策略拒绝覆盖。

浏览器面板只监听随机端口的 `127.0.0.1`，与 MCP 服务同一进程。它不是 MCP Streamable HTTP，也不向局域网提供 MCP 接入。MCP、CLI、用户浏览器应在同一控制端电脑上运行；SSH 容器/远程宿主的浏览器转发不属于此本地接入模式。

页面标题为 CrossDesk MCP，仅保留正式应用标志，使用文字按钮和简洁状态提示。布局填满浏览器或宿主面板的可用宽高，远程画面保持原比例自动缩放；连接设置在高度不足时独立滚动。连接后侧栏显示设备与授权信息，暂停、恢复和断开按钮位于画面工具栏。「放大画面」收起侧栏，按 Esc 还原；窄屏默认显示连接表单，连接后显示画面，可用「连接信息」展开设备详情。界面资源均可离线使用，保存设备与持久授权仍由用户分别选择。

首次打开使用 URL fragment 中的短期一次性 bootstrap，页面立即清除 fragment，并换取 HttpOnly/SameSite cookie 和 CSRF 令牌。模型只收到无特权的网页地址。复制无凭据地址到另一浏览器不能获得会话控制权。

无法自动打开浏览器时，在自己的终端运行工具结果 `pairing_command` 中的命令和参数。例如：

```sh
crossdesk-mcp setup --open-panel --view <view_id> --config-dir <相同的配置目录>
```

通过源码运行时用 `node /absolute/path/apps/mcp/dist/main.js` 替换 `crossdesk-mcp`。配对通过本用户配置目录中的受限运行描述符和本地 IPC 完成，不允许匿名 HTTP 请求签发控制凭证。`setup` 使用用户终端的桌面环境打开浏览器；仍无法打开时，只在该交互终端显示 60 秒有效的一次性地址，不将它返回 MCP 或发送到聊天。面板最多保留 32 个视图；再次打开时会清理超过 30 分钟未使用的视图。浏览器会话最长 12 小时，失效后重新打开。

## 无面板配置与凭据

在用户自己的交互终端执行（不能在宿主 stdio 通道中交互输入密码）：

```sh
crossdesk-mcp setup
```

配置默认存放于 `~/.config/crossdesk/mcp/`，可用 `--config-dir` 或 `CROSSDESK_MCP_CONFIG_DIR` 覆盖。`devices.json` 包含设备、面板/视频选项及凭据引用；密码保存到 macOS Keychain、Windows 凭据库或 Linux Secret Service。凭据库不可用就返回明确错误，没有明文文件降级。面板的临时连接仍可使用用户现场输入的密码。持久保存失败会保留已建立连接，并在面板显示错误。

也可在 `setup` 中指定外部密码环境变量名称，服务读取该变量作为凭据提供器。示例配置（没有密码值）：

```json
{
  "version": 1,
  "panel": "off",
  "server": "api.crossdesk.cn",
  "port": 9099,
  "devices": [{
    "alias": "work-pc",
    "device_id": "123456789",
    "server": "api.crossdesk.cn",
    "port": 9099,
    "authorization": "control",
    "credential": "environment",
    "environment_variable": "MY_CROSSDESK_PASSWORD"
  }]
}
```

授权范围和可用凭据缺一不可。模型不能提供任意 `device_id` 绕过面板确认；模型侧 `crossdesk_connect` 仅接受已授权设备别名。不要把密码发给 Agent，也不要把包含密码的命令行写进宿主配置。

Apps 依赖宿主遵守 app-only 工具可见性和 `_meta` 的模型不可见约定；服务另检查每个视图的随机 UI 能力令牌和 generation。app-only 标记本身不是可信身份凭据。能够读取同用户进程/文件或执行任意本机代码的 Agent 不受此边界隔离。

## 模型工具

| 工具 | 作用 |
| --- | --- |
| `crossdesk_open` | 打开设备选择/连接面板，返回 `view_id` |
| `crossdesk_open_browser` | 用户显式请求的浏览器入口/回退 |
| `crossdesk_list_devices` | 本机设备簿和授权摘要，不提供云发现 |
| `crossdesk_connect` | 通过别名连接已经授权且有凭据的设备 |
| `crossdesk_get_session` | `session_id`、`view_id` 严格二选一；按 `view_id` 可等待连接，`wait_ms` 范围 0–25000 |
| `crossdesk_screenshot` | 返回 PNG image content 和观测元数据 |
| `crossdesk_action` | 结构化输入，可附带动作之后的截图 |
| `crossdesk_set_control` | 在已授权范围内启停 Agent 输入 |
| `crossdesk_select_display` | 切换显示器，使旧观测失效 |
| `crossdesk_disconnect` | 断开并释放输入状态、子进程和设备锁 |

动作包括 `move`、`click`、`double_click`、`scroll`、`drag`、`key_press`、`hotkey`、`type_text`。不提供 shell、任意键按下保持、剪贴板或文件操作。按键使用 Windows VK 对应的标准名称：`A`–`Z`、`0`–`9`、`F1`–`F12`、`CTRL`、`ALT`、`SHIFT`、`META`、`ENTER`、`ESC`、方向键等；应用快捷键仍需根据远端系统选择。

鼠标动作和 `key_press` 支持可选的 `modifiers: ["CTRL", "ALT", "SHIFT", "META"]`（选择需要的键，不能重复），用于组合点击、选择、拖拽和快捷键。修饰键仅在本次动作期间保持，结束、取消或人工暂停时释放。`drag` 可指定 `button: "left" | "right" | "middle"`，默认左键。标点键支持 `SEMICOLON`、`EQUAL`、`COMMA`、`MINUS`、`PERIOD`、`SLASH`、`BACKQUOTE`、`BRACKET_LEFT`、`BACKSLASH`、`BRACKET_RIGHT`、`QUOTE`；这些是 VK 键位名称，实际字符取决于远端键盘布局，输入文字应使用 `type_text`。例如：

```json
{"session_id":"…","operation_id":"select-item-1","observation_id":"…","action":{"type":"click","x":120,"y":80,"modifiers":["SHIFT"]}}
```

截图默认最长边 1280，最大 2048；允许指定源帧像素区域。返回 `source_size`、`source_rect`、`image_size`、`generation`、`geometry_epoch`、`frame_id`、`frame_age_ms` 和 `observation_id`。动作坐标使用返回图像坐标，不使用浏览器 CSS 像素。观测保留最多 32 个，30 秒过期；切换显示器、连接代际或图像几何变化立即失效。截图使用不超过 2 秒的帧，PNG 不超过 2 MiB，必要时继续缩小输出并返回真实尺寸。

`timeout_ms` 限制等待可用帧的时间，PNG 编码独立执行；`0` 表示只使用当前已就绪的新鲜帧，无帧返回 `FRAME_TIMEOUT`，编码器繁忙返回 `BUSY`。截图按到达顺序处理，并优先于预览。视频超过 2 秒未更新时拒绝新的键鼠输入，恢复新鲜画面后可继续；已提交操作的去重查询仍可读取原结果。

`operation_id` 在当前原生会话中缓存最近 128 个操作，至少在未被容量淘汰的 5 分钟窗口内支持同参数查询；不同参数复用 ID 返回冲突。`submitted` 仅说明本地发送完成，远端没有执行 ACK。超时、断网、取消或发送失败可能返回 `outcome_unknown`/`side_effect_possible`，禁止自动重放。`observe_after` 默认开启，后续图像只证明取得更新帧，不证明操作执行成功。

## 实时视频与按需截图

实时视频提供两条路径，默认 `browser`，两条路径均由原生 CLI 的本机 WebSocket 直接发送给浏览器。Node 只处理会话、授权票据和 MCP 工具，不读取或转发实时视频字节；不再创建视频管道，不经过 MCP stdio 或模型上下文，不逐帧转 PNG、不重新编码视频。

| `video.decode_path` | 画面路径 |
| --- | --- |
| `browser` | MiniRTC 输出原始 H.264 Annex B → 浏览器 WebCodecs 解码 → canvas |
| `minirtc` | MiniRTC 解码 → 紧凑 NV12 原始帧 → 浏览器 VideoFrame / canvas；浏览器不再解码 H.264 |

`video.decoder` 默认 `auto`：优先使用可用硬件解码器，不支持时使用软件。`software` 禁用本程序的硬件选择与硬件回退。浏览器路径使用 WebCodecs 的 `prefer-hardware` / `prefer-software`；这些值是浏览器 API 的偏好，浏览器可以忽略，不能据此宣称已确认物理硬件或软件后端。原生路径由 MiniRTC 的解码器工厂选择，本机 macOS 使用 VideoToolbox 或 OpenH264；其他平台使用 MiniRTC 已支持的后端。状态中的 `native_video_decoder` 返回 MiniRTC 报告的后端名称（统计可用前为空），不代表浏览器后端。

在 `~/.config/crossdesk/mcp/devices.json` 的根对象中加入：

```json
"video": {
  "decode_path": "minirtc",
  "decoder": "auto"
}
```

也可在 Agent 宿主的 MCP 启动参数中配置，命令行优先于文件：

```sh
crossdesk-mcp serve --stdio --video-path browser --video-decoder auto
crossdesk-mcp serve --stdio --video-path minirtc --video-decoder software
```

修改后重启 MCP，再建立连接。选项适用于整个 MCP 进程，两条路径均将解码选项传给原生端；浏览器路径中的原生解码用于截图缓存。截图工具或动作后的截图请求才触发 PNG 编码。这里配置的是控制端解码器，不更改远端编码器。原有配置文件无需新增字段即可继续使用。

Agent 模式默认请求高帧率档位，分辨率上限 1920 × 1080；实际分辨率和帧率由远端能力、网络和本机解码性能决定。面板显示实际尺寸与呈现 FPS。浏览器路径需要 WebCodecs H.264 解码能力；MiniRTC 路径仅需浏览器支持从 NV12 创建 VideoFrame 并绘制。能力不足时明确报错，不自动退回 PNG 轮询。NV12 不压缩，1080p / 30 FPS 约需 93 MB/s 本机传输，4K / 30 FPS 约需 373 MB/s；它不增加远端到控制端的 H.264 网络码率。移除 Node 转发没有消除 NV12 自身的本机传输与浏览器像素处理开销；这不是 GPU 零拷贝。

视频 WebSocket 在原生 CLI 的独立 I/O 线程中运行，只监听随机 `127.0.0.1` 端口，与 Node 面板 HTTP 端口不同。浏览器面板通过 cookie/CSRF 授权获取视频票据，Apps 通过 app-only `crossdesk_video_open` 和视图令牌获取。票据 15 秒内一次性有效，绑定视图、会话和代际，通过 WebSocket 第一条消息提交，不写入 URL、日志或模型可见工具结果。WebSocket 只提供画面、收帧 ACK 和关键帧恢复请求，不能执行远端输入操作。最多 8 个活动观看者。H.264 丢帧后等待关键帧恢复；NV12 每帧独立，每个观看者最多一帧未确认，原生发送队列只保留最新画面；H.264 最多 8 帧未确认并受字节阈值限制。10 秒未确认最早一帧则关闭该视频通道，不阻塞其他观看者或 MCP 控制。视图撤销、显示器/连接代际变化、断开和进程退出会关闭对应视频连接；暂停 Agent 控制仍继续播放。页面隐藏或卸载时释放视频订阅，恢复可见时重新订阅。

浏览器页面和 MCP Apps 资源的 CSP 允许原生服务使用的 `ws://127.0.0.1:*`（仅动态端口），HTTP 控制仍限制到 Node 面板的确切地址。原生服务另外校验确切 Host、Origin 和一次性票据。宿主仍可能限制 loopback、WebSocket、WebCodecs 或 iframe 权限；视频失败时提供重试和浏览器入口，`--panel=apps` 的严格策略仍禁止浏览器回退。浏览器和 Apps 共用同一播放器，具体宿主需要独立验收。

## 打包

```sh
npm run build
npm run pack:npm -- --out-dir /absolute/path/artifacts
```

脚本生成可发布 tarball，包含面板、运行代码、源代码、许可和固定版本的五个平台可选依赖。CI 的 `MCP package` 工作流仅构建并上传 `mcp-package`，不发布 npm。原生包继续由 CLI 工作流构建，`doctor` 必须同时声明自动化协议和 `automation_video_protocols: ["h264-annexb/1", "nv12/1"]` 和 `automation_video_decoders: ["auto", "software"]`，以及 `automation_video_transport: "loopback-websocket/1"`、`automation_terminal_protocol: "ansi/1"`；先发布对应 0.2.0 平台包，再发布 MCP tarball。尚未进行真实跨平台发布/远端验收时，不应把构建成功等同于各宿主和设备上的联调完成。

从发布 tarball 重建 TypeScript/面板时，将包中的 `build-package.json` 复制为 `package.json`，运行 `npm ci && npm run build`。原生源码和依赖来源随平台包提供，详见 [源码来源](../cli/docs/source-provenance.md)。

## 本地审阅验证（2026-11-03）

动作参数校验和输入步骤生成集中在 `apps/cli/automation_protocol.*`，主循环负责调度、取消和权限检查。截图按色度网格对齐后仅转换所需区域，PNG 在容量受限的缓冲区中单次压缩；原生帧仅在没有其他读取者时复用内存。Node 按行累计响应字节，在完整响应到达时解码，避免反复扫描和复制大截图。

本机 macOS arm64 完成 TypeScript/面板构建、原生 CLI 构建、现有 wire/keyboard-state 测试，以及工程外的 MCP、会话和原生动作回归检查。ASan/UBSan 检查覆盖动作调度和输入清理；140 组随机图像覆盖完整帧、奇偶裁剪边界、缩放及容量回退，优化前后 PNG 字节一致。

使用 1920 × 1080 合成 NV12、最长边 1280、每项 6 次的 `-O2` 本地基准：完整画面编码约 22.5 → 11.5 ms，321 × 241 裁剪约 2.1 → 0.9 ms。数字仅衡量图像转换与编码，不包含网络、解码、远端执行或宿主渲染；未连接真实远端或完成 Windows/Linux 验收。额外测试代码、脚本、数据和产物均存放于工程外系统临时目录。
