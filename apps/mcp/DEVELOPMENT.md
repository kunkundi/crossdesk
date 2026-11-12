# CrossDesk MCP 开发与集成参考

本文面向开发者与宿主集成者，记录构建、工具接口、显示机制和验证范围。安装与日常使用请参阅 [用户指南](README.md)。

导航：[源码构建](#从源码运行) · [DSH 开发配置](#harness-development) · [工具接口](#模型工具) · [远程操作](#远程操作) · [实时视频](#实时视频与按需截图) · [打包](#打包)

## 版本与依赖

CLI、MCP 包 `@chunkdash/crossdesk-mcp` 和内置 DSH 插件共用版本，当前均为 0.2.0，MCP 原生依赖也固定为 0.2.0。使用[版本脚本](../../docs/VERSIONING.md)修改任一工具版本会同步整个分组。源码版本和本地打包不代表版本已经发布到 npm；内含原生程序的平台安装包可独立分发。

当前提供 31 个模型工具和 11 个 MCP Apps 面板专用接口。本机 MCP 接入使用 stdio，不提供 MCP Streamable HTTP 或通用远程 shell 工具。控制端原生 CLI、被控端和信令服务器需要支持对应自动化协议。

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

完整编译环境见 [源码构建指南](../../docs/BUILD.md)。`npm run build` 同时构建 MCP、页面和 DSH 配套插件。源码目录的 `npm ci` 只安装开发依赖，**源码运行必须显式指定原生程序路径**；生成的发布包才声明固定版本的平台依赖。

例如，macOS arm64 Release 构建后可先检查：

```sh
/absolute/path/crossdesk/build/macosx/arm64/release/crossdesk-cli doctor --json
```

Windows、Linux、x64 或 Debug 构建使用自己的实际产物路径。`--native-binary` 必须指向可执行文件，不接受 shell 命令；也可用 `CROSSDESK_NATIVE_BINARY` 指定。原生程序需要提供 MCP 所需的 automation、terminal、video 协议；仅能运行交互式 CLI 不代表版本兼容。

两种安装方式在下文使用的路径不同：

| 项目 | 本地安装包 | 源码构建 |
| --- | --- | --- |
| MCP 入口 | `<安装目录>/node_modules/@chunkdash/crossdesk-mcp/dist/main.js` | `<工程目录>/apps/mcp/dist/main.js` |
| DSH 插件目录 | `<安装目录>/node_modules/@chunkdash/crossdesk-mcp`（也可直接安装 `.tgz`） | `<工程目录>/apps/mcp` |
| 原生程序 | 平台包内置，通用包使用固定版本平台依赖，或显式指定路径 | 显式指定构建产物路径 |

服务启动不连接远端、不打开浏览器；Agent 调用打开或连接工具后才进入授权与连接流程。

### Codex 源码配置

按 [Codex 安装指南](README.md#install-codex) 配置服务时，源码用户将 `args` 替换为以下内容；原生路径示例对应 macOS arm64 Release，其他构建替换为自己的产物路径：

```toml
args = [
  "/absolute/path/crossdesk/apps/mcp/dist/main.js",
  "serve", "--stdio",
  "--panel=browser", "--browser-launch=host",
  "--native-binary", "/absolute/path/crossdesk/build/macosx/arm64/release/crossdesk-cli"
]
```

<a id="harness-development"></a>

## DSH 插件开发与配置

源码用户先完成上方 [源码构建](#从源码运行)，再安装 **MCP 根目录**：

```sh
dsh plugin --profile desktop add /absolute/path/crossdesk/apps/mcp
```

源码目录不内含原生程序，需要在 `~/.dsh/profiles/desktop/cordis.patch.yml` 中为 `crossdesk` 添加配置；使用自定义 `DSH_HOME` 或 profile 时改为对应路径。将条目合并到现有列表，原生路径替换为本机实际构建产物：

```yaml
- id: crossdesk
  config:
    nativeBinary: /absolute/path/crossdesk/build/macosx/arm64/release/crossdesk-cli
```

插件配置只需在修改默认值时填写：

| 字段 | 默认值 | 用途 |
| --- | --- | --- |
| `nativeBinary` | 包内原生程序，或固定版本的平台依赖 | 源码开发或显式使用兼容 CLI，必须为绝对路径 |
| `configDir` | `~/.config/crossdesk/mcp/` | 指定设备配置目录，必须为绝对路径 |
| `serverName` | `crossdesk` | MCP 工具命名空间；侧栏会同步使用该名称 |

用户配置覆盖整个 `config` 时需保留仍然需要的字段。Windows 路径可用 `C:/.../crossdesk-cli.exe`。集成机制参考 [Harness bundle](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/boot/app-boot#profiles) 与 [MCP Client](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/mcp/mcp-client)。

DSH 源码位于 `integrations/deepseek-harness/src/`：`plugin.js` 管理原生检查和 MCP 生命周期，`host.js` 交接工具结果，`client.js` 打开对应对话的侧栏。构建入口生成 `dist/harness.js` 和 `dist/harness-client.js`，包根目录的 `cordis.patch.yml` 注册 `crossdesk` 组件。宿主提供 MCP Client，复用其工具权限、命名空间保留、图像处理和连接清理行为。

## 通用包与原生依赖

如果已获得维护者或 CI 构建的 `chunkdash-crossdesk-mcp-0.1.0.tgz`，在固定目录安装。可在 [Build MCP 工作流](https://github.com/kunkundi/crossdesk/actions/workflows/build-mcp.yml) 的成功运行中查找 `mcp-package` 产物；Actions 下载件是外层 ZIP，先解压，再将其中的 `.tgz` 交给 npm；不要把 ZIP 直接交给 npm。没有可用安装包时，使用下方源码构建流程。

macOS / Linux：

```sh
mkdir -p "$HOME/crossdesk-mcp"
cd "$HOME/crossdesk-mcp"
npm init -y
npm install /absolute/path/chunkdash-crossdesk-mcp-0.1.0.tgz
node node_modules/@chunkdash/crossdesk-mcp/dist/main.js --help
```

Windows PowerShell：

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE/crossdesk-mcp"
Set-Location "$env:USERPROFILE/crossdesk-mcp"
npm init -y
npm install "C:/Downloads/chunkdash-crossdesk-mcp-0.1.0.tgz"
node node_modules/@chunkdash/crossdesk-mcp/dist/main.js --help
```

安装包已经包含页面和 DSH 插件构建产物，无需再执行 `npm run build`。从 CLI 构建工作流取得的 `crossdesk-mcp-<平台>` 产物还内含对应平台的原生 CLI，安装这类包无需另装 CLI；以下补充步骤只针对不带原生程序的通用 `mcp-package`。安装通用包时保留 optional dependencies：包会尝试安装固定版本的平台原生依赖，支持 macOS arm64/x64、glibc Linux arm64/x64 和 Windows x64。当前 MCP 0.1.0 绑定原生包 0.2.0；如果对应原生包尚未发布或下载失败，需要在**同一安装目录**安装维护者提供的对应平台包，例如 macOS arm64：

```sh
npm install /absolute/path/chunkdash-crossdesk-cli-darwin-arm64-0.2.0.tgz
```

也可在下文宿主配置中添加 `--native-binary`，指向已经构建好的兼容 `crossdesk-cli` 可执行文件（Windows 为 `.exe`）。全局安装 CLI 不会被 MCP 自动发现：未指定路径时先使用包内对应平台的原生程序，否则解析固定版本的平台依赖，不搜索 PATH。`--help` 仅验证 Node 入口；普通 stdio 服务在连接时检查原生协议，DSH 合并插件则在启动时提前检查。

如果使用 npm 仓库，先确认所需版本已发布，再将上述安装命令替换为 `npm install @chunkdash/crossdesk-mcp@<已发布版本>`；仓库里的版本号不代表 npm 上已有该包。

## 用户连接流程

1. 对 Agent 说“打开 CrossDesk”。`crossdesk_open` 创建 `view_id`，返回面板选择和等待用户状态。
2. 在面板选择已保存设备，或输入 9 位设备 ID；密码填写在面板里。可选择仅查看或允许 Agent 控制。
3. “保存设备”“记住密码”“允许以后自动连接”分别控制设备簿、系统凭据库、持久权限。后两项要求保存设备，默认均关闭。保存密码不自动授予控制权限。
4. 有待执行的远程任务时，Agent 在打开面板后立即调用 `crossdesk_get_session({view_id, wait_ms: 25000})` 等待连接。连接就绪后，工具直接返回会话 ID 和权限状态，Agent 继续任务；用户无需回到对话发送连接确认，也无需点击继续按钮。浏览器和 Apps 面板使用同一流程。
5. Agent 获取独立截图，用截图的 `observation_id` 和图像坐标提交动作。面板预览不消耗模型截图序列。

`crossdesk_open` 立即返回，让 Apps 宿主有机会渲染面板；等待连接在后续 `crossdesk_get_session` 调用中完成。按 `view_id` 查询默认最多等待 25 秒，`wait_ms: 0` 则立即查询；未就绪时返回 `wait_timed_out: true`，Agent 在原任务仍待执行时继续等待。按 `session_id` 查询默认立即返回，传入正数 `wait_ms` 可等待正在进行的连接恢复；带 `unlock_request_id` 时等待对应解锁请求。取消等待只取消本次查询，不断开用户正在建立或使用的连接。用户主动断开后返回 `closed`，Agent 停止等待，不自动重连。

面板发起的连接失败后，按 `view_id` 等待也会返回 `failed` 和错误码，避免 Agent 一直停留在等待用户状态。断开正在建立的连接会取消整个连接尝试。多个面板和 Agent 同时查询状态时共用一次原生查询，单次查询取消或暂时繁忙不会断开会话。`crossdesk_connect` 在连接成功但面板初始化失败时仍返回有效的 `session_id`，并附加 `panel_error`。

初次信令注册使用原生连接的整体 30 秒期限，期间允许 MiniRTC 按退避策略恢复临时传输异常。期限覆盖所有初始重试。证书校验失败、登录被拒绝，以及设备连接请求发出后的信令中断会结束当前尝试。连接失败结果使用 `error.code` 表示错误类别，并以 `error.reason` 提供已识别的原生原因，例如 `signaling_timeout`、`signaling_disconnected` 或 `device_offline_or_unavailable`。面板显示对应说明，终端错误包含原因标识。`logs/<session_id>/transport.log` 记录连接阶段、受控的传输错误分类与数字码、连接关闭码和重试退避信息。

自动继续依赖宿主保持当前 Agent 任务及工具调用流程。它不通过网页伪造用户消息，也不承诺从已经结束的对话中主动唤醒 Agent；此类唤醒由宿主负责。

人工暂停由原生层锁定，模型不能通过 `crossdesk_set_control({session_id, enabled: true})` 恢复；只有当前授权面板的恢复按钮可以恢复。浏览器和 Apps 面板还支持人工接管键鼠，并由用户交还 Agent。仅查看连接不能升级为控制连接，需重新连接并授权。关闭/隐藏面板会停止预览并释放面板持有的按键，远程会话保留；断开按钮或 `crossdesk_disconnect` 才结束会话。

同一 MCP 进程支持最多八个并行设备连接，面板按设备绑定会话。相同配置目录中的进程用设备锁避免同时连接同一目标；它不协调另一个系统用户、另一份配置目录或独立桌面客户端。

## 面板策略

`--panel` 优先于设备配置中的 `panel`，默认 `auto`；`--browser-launch` 同样默认 `auto`。MCP 根据客户端标识内置 Codex 和 DeepSeek Harness 的默认策略，不依赖个人 Codex 配置或 `AGENTS.md`。已有配置中显式指定的面板模式和启动方式仍然有效。

| 模式 | 行为 |
| --- | --- |
| `auto` | Codex 和 DeepSeek Harness 默认选择宿主侧栏浏览器；其他客户端有本机 GUI 时使用系统浏览器，无 GUI 且有终端环境时提供 CLI 画面入口，两者均无则使用 `off` |
| `apps` | 只使用 MCP Apps；能力不匹配返回 `PANEL_UNSUPPORTED` |
| `browser` | 使用本地浏览器页面；启动方式为 `auto` 时 Codex 和 DeepSeek Harness 使用宿主侧栏，其他客户端使用系统浏览器；可显式选择 `--browser-launch=host` 或 `--browser-launch=system`，不附加 Apps 工具资源元数据 |
| `terminal` | 提供独立终端 `view` 命令，共用当前 MCP 会话，不启动网页服务 |
| `off` | 不启动网页服务，保留截图和操作工具，凭据通过独立 `setup` 配置 |

`auto` 不选择 MCP Apps；需要宿主内嵌面板时显式使用 `--panel=apps`。Apps 使用 `extensions["io.modelcontextprotocol/ui"].mimeTypes` 中的 `text/html;profile=mcp-app` 判断能力。旧协议读取 initialize 能力；2026-07-28 协议读取当前请求的能力，不按宿主产品名猜测支持情况。Apps 的卡片、侧栏或窗口形式由宿主决定。

除 Codex 和 DeepSeek Harness 外，客户端使用 `auto` 时，Linux 根据 `DISPLAY` / `WAYLAND_DISPLAY` 判断 GUI；macOS 检查当前用户的桌面登录会话，SSH 会话不自动打开远端 Mac 的浏览器；Windows 排除 SSH 和 Services 会话。终端判断包含 TTY、`SSH_TTY` 和非 `dumb` 的 `TERM`，因为 MCP 的 stdin/stdout 通常是管道。需要终端或无面板运行时，显式设置 `--panel=terminal` 或 `--panel=off`。

终端模式的 `crossdesk_open` / `crossdesk_connect` 返回 `terminal_command`。用户在同一主机的另一个交互终端执行（源码运行时将 `crossdesk-mcp` 替换为 `node /absolute/path/apps/mcp/dist/main.js`）：

```sh
crossdesk-mcp view --view <view_id> --config-dir <相同的配置目录>
```

该命令提供设备选择、隐藏密码输入和本次控制授权；已建立会话则直接显示画面。终端通过受限配置目录中的运行描述符和认证本地 IPC 复用 MCP 的原生连接，不启动第二个远程连接，也不占用 Agent 的终端或 MCP stdio。Agent 应展示命令供用户执行，再通过 `crossdesk_get_session` 等待；不能在工具输出捕获通道中运行 `view`。无交互终端时 `view` 返回 `VIEW_REQUIRES_TERMINAL`。

终端画面采用原生 CLI 的 ANSI 四分块渲染器，目标 8 FPS，最多 160 列 × 60 行，根据终端环境使用 8/256/真彩色。默认字符宽高比 0.5，可用 `--cell-aspect`（0.2–2）调整。此入口暂不使用 Kitty/Sixel。`p` 暂停 Agent、`r` 恢复已授权控制、`d` 断开、`q` / Ctrl+C 仅关闭画面并恢复终端；关闭画面后会话保留。暂停仍由原生层锁定，Agent 不能自行恢复。

浏览器/Apps 与终端均绘制 Agent 最后成功发送的鼠标位置，支持移动、点击、滚动和拖拽；坐标来自原生层对截图缩放及裁剪的映射。暂停或关闭控制时隐藏，切换显示器或图像尺寸变化时清除。该指针是 Agent 的操作位置提示，不代表远端输入执行 ACK，也不跟踪远端用户自己的鼠标。它不写入模型截图或源视频。

浏览器及 Apps 的实时视频光标会以 60–140 毫秒的短缓动追踪最新坐标，动画独立于视频帧刷新；连续位置更新从当前显示位置接续，不排队回放旧轨迹。首次显示、连接代际或显示几何变化时直接定位，隐藏或停止播放时取消动画，并遵守系统的“减少动态效果”设置。平滑效果只作用于页面提示光标，原生鼠标输入及点击坐标不受影响，因此不表示远端鼠标实际经过了补间路径。

实时视频光标采用 CrossDesk 主色（`#3564e3`）的圆角箭头、白色描边和同色柔光，与客户端及页面配色一致。移动时尖端朝向页面上实际移动的方向，以尖端为旋转锚点；方向按画面像素计算，适配宽屏与竖屏。停止后保留最后朝向，忽略小于半个 CSS 像素的位移对朝向的影响；首次显示及连接、显示几何变化时恢复默认朝向。

Apps 资源为 `ui://crossdesk/viewer.html`，工具只在允许使用 Apps 且当前请求支持时暴露 UI 元数据和面板专用接口。页面回报 `crossdesk_ui_ready` 后才标记就绪；10 秒未回报显示 `not_confirmed`，不自动弹第二个浏览器。用户明确要求回退时调用 `crossdesk_open_browser`；该工具不附带 Apps 元数据，`apps`/`off` 策略拒绝覆盖。

浏览器面板只监听随机端口的 `127.0.0.1`，与 MCP 服务同一进程。它不是 MCP Streamable HTTP，也不向局域网提供 MCP 接入。MCP、CLI、用户浏览器应在同一控制端电脑上运行；SSH 容器/远程宿主的浏览器转发不属于此本地接入模式。

页面以 Agent 会话监看为中心：首次连接显示授权卡片，连接后远程画面占满可用区域，设备、权限和恢复设置收进「会话详情」抽屉。工具栏显示设备与实际控制状态，仅在具有控制授权时保留「暂停 Agent / 恢复 Agent」；本面板人工接管时改为「交还 Agent」。画面放大、人工接管、声音与音量、断开连接收进「会话选项」，仅查看会话不显示人工接管入口。等待系统解锁时显示独立提示及密码入口，关闭解锁弹窗后仍可再次打开。状态区分仅查看、控制已启用、已暂停、人工接管和恢复连接，不将控制权限解释为 Agent 正在执行任务。

页面标题为 CrossDesk MCP，页内使用 CrossDesk 正式标志和名称。配色与桌面客户端的 WorkbenchStyle / ButtonStyle 一致，采用浅蓝灰底色、蓝色主操作、6 像素控件圆角及 12 像素卡片圆角；工具栏按钮统一为 32 像素高，图标按钮保留可访问名称与悬停提示。布局填满浏览器或宿主面板的可用宽高，远程画面保持原比例缩放，底部集中显示视频状态、分辨率和帧率；授权卡片在高度不足时独立滚动，窄侧栏沿用相同的监看布局。「放大画面」隐藏页眉并缩小外边距；退出菜单、详情和非人工接管状态下的放大模式支持 Esc。界面资源均可离线使用，保存设备与持久授权仍由用户分别选择。

首次打开使用 URL fragment 中的短期一次性 bootstrap，页面立即清除 fragment，并换取 HttpOnly/SameSite cookie 和 CSRF 令牌。原始 bootstrap 不进入工具结果；`browser_url` 不含配对凭据，`browser_launch_url` 则是短期配对入口，只用于交给宿主导航，不应读取或发布。复制无凭据地址到另一浏览器不能获得会话控制权。

无法自动打开浏览器时，在自己的终端运行工具结果 `pairing_command` 中的命令和参数。例如：

```sh
crossdesk-mcp setup --open-panel --view <view_id> --config-dir <相同的配置目录>
```

通过源码运行时用 `node /absolute/path/apps/mcp/dist/main.js` 替换 `crossdesk-mcp`。配对通过本用户配置目录中的受限运行描述符和本地 IPC 完成，不允许匿名 HTTP 请求签发控制凭证。`setup` 使用用户终端的桌面环境打开浏览器；仍无法打开时，只在该交互终端显示 60 秒有效的一次性地址，不将它返回 MCP 或发送到聊天。面板最多保留 32 个视图；再次打开时会清理超过 30 分钟未使用的视图。浏览器会话最长 12 小时，失效后重新打开。

### Codex 默认侧栏浏览器

安装、配置示例及首次使用见 [Codex 用户安装与使用](README.md#install-codex)。任何用户在 Codex 中直接启动 MCP，默认都会返回侧栏浏览器入口；以下说明宿主识别和配对机制。

MCP 识别 Codex 的客户端名称 `codex-mcp-client` 和 DeepSeek Harness 的 `dsh-mcp-client`：旧协议读取 initialize 中的 `clientInfo`，2026-07-28 协议读取当前请求的 `io.modelcontextprotocol/clientInfo`。该标识仅选择显示方式，不授予任何设备权限。`--panel=auto` 在这两个宿主中选择 `browser`，`--browser-launch=auto` 选择 `host`；其他客户端仍沿用环境检测和系统浏览器。

`host` 不启动系统浏览器，也不生成 MCP App 卡片；它要求宿主浏览器与 MCP 运行在同一台电脑。Codex 使用本机 `file://` 启动页；DeepSeek Harness 使用下述 HTTP 启动入口。显式使用 `--panel=browser --browser-launch=host` 可以强制宿主浏览器，使用 `--panel=browser --browser-launch=system` 则强制系统浏览器。显式将 `--browser-launch=host` 与 `apps`、`terminal` 或 `off` 组合会报错，这些模式单独使用不受浏览器默认值影响。

服务指令、工具说明和返回值均内置 Codex 的打开流程：Agent 拿到 `browser_launch_url` 后立即打开宿主浏览器，即使连接已就绪也不省略此步骤。无需用户再次要求打开侧栏，也无需添加个人 `AGENTS.md`。实际打开仍由宿主浏览器工具执行，MCP 协议本身不能强制所有客户端展示侧栏；缺少该工具时（例如没有侧栏浏览器的 Codex CLI）应明确说明限制并展示本地配对命令。


`crossdesk_open`、`crossdesk_connect` 和 `crossdesk_open_browser` 在此模式返回 `browser_launch_url`、`browser_url`、`browser_host`、剩余有效时间及绝对到期时间。Agent 应将 `browser_launch_url` 直接交给宿主的浏览器打开工具；在 Codex 中调用 `open_in_codex`，设置 `target.type` 为 `browser`、`target.url` 为该地址、`placement` 为 `right`。不要读取、粘贴或上传启动页内容。

Codex 启动页位于系统临时目录的独立目录中，POSIX 权限为目录 `0700`、文件 `0600`，内含 60 秒有效的一次性配对地址。浏览器在本机跳转到播放器并换取自己的 HttpOnly/SameSite cookie 和 CSRF 令牌；工具结果不包含原始 bootstrap 票据。票据绑定视图及代际，消费、到期或正常关闭服务时清理启动页。启动地址过期时，对已有 `view_id` 调用 `crossdesk_open_browser` 重新签发，不要重新连接远端。

`browser_url` 是不带配对凭据的本机 HTTP 地址，用于已配对浏览器重新打开；直接复制到另一浏览器不能获得当前会话权限。没有内置浏览器的宿主可由用户执行原有 `pairing_command`，在系统浏览器完成配对。

侧栏中的网页沿用本机 WebSocket → WebCodecs/VideoFrame 播放路径，媒体数据不经过 MCP 工具结果，也不需要新增公网中转。它与 MCP Apps iframe 是不同的运行环境；是否能播放及实际帧率需要在目标宿主验证。

2026-11-11 在本机 Codex 侧栏浏览器完成实测：通过上述启动页连接 macOS 远端，页面显示“实时”、1920 × 1080、约 26 FPS，画布帧计数持续递增。原生端确认 `h264-annexb/1` 经 `loopback-websocket/1` 传输，1 个活动观看者、发送队列无积压；验证期间仅查看画面，未发送远端输入。此结果验证当前环境下的侧栏实时播放，实际帧率仍取决于远端、网络和解码性能。

2026-11-12 使用工程外的全新临时配置完成 16 项 stdio 协议检查：Codex 新旧协议均在不设置面板参数时返回宿主浏览器启动页，其他客户端保留原有行为，显式面板及启动方式覆盖有效，重新签发及退出清理正常；另以模拟会话验证已就绪连接和解锁面板的 Codex 路由。此检查不读取个人 Codex 配置，不连接真实远端。

### DeepSeek Harness 桌面侧栏

Harness 桌面版的 Sidebar Browser 只接受 HTTP(S) 地址，不能打开 Codex 的 `file://` 启动页；其内置侧栏也未提供供 Agent 调用的打开工具。安装合并的 CrossDesk MCP 插件后，工具调用会自动打开当前 Harness 会话的浏览器侧栏，不需要手动点击链接。代码结构见 [DSH 集成说明](integrations/deepseek-harness/README.md)。

完整安装与配置见 [DeepSeek Harness 用户安装与使用](README.md#install-dsh)。合并插件管理 MCP 进程和页面显示，不增加设备权限，也不会发起第二次远端连接。

Harness 的 `browser_launch_url` 为本机 `/launch/<随机标识>`，在 60 秒内仅可导航一次，再跳转到原有配对流程；原始 bootstrap 令牌不进入工具结果。启动地址本身是短期配对入口，不应发布、抓取或记录其内容。消费、过期、视图代际变化或服务关闭后均不可重用；过期时为已有 `view_id` 重新签发。现有 Host/Origin 校验、HttpOnly/SameSite Cookie、CSRF 和禁止 iframe 嵌入的策略保持有效。

侧栏适配只在 Harness 桌面版启用；Harness 网页版的 iframe 不支持此路径，不会为此开放任意跨域嵌入或公网转发。未安装插件时，Agent 应说明安装方式，或提供入口链接供用户选择“在侧栏打开”，不能声称已自动打开。侧栏导航成功与配对/视频就绪分别确认，Agent 仍须等待 `crossdesk_get_session`。

2026-11-12 在 macOS 的 DeepSeek Harness 0.2.2-rc.1 完成实测：安装配套插件后，`crossdesk_connect` 以仅查看模式连接已保存的 Mac mini，插件自动打开当前会话右侧浏览器并完成配对；页面显示实时画面、1920 × 1080、约 27–29 FPS。验证过程中未发送远端键鼠输入。另在工程外临时目录通过 23 项协议/配对/宿主桥接检查及 6 项客户端生命周期检查，覆盖 Codex 与 Harness 新旧协议、单次消费、过期、代际失效、会话隔离、多窗口领取和标签复用。

同日完成合并包验证：使用 Harness 自带 CLI 在工程外全新 profile 中仅安装一个 macOS arm64 平台包，确认 bundle 和配置 schema 可加载；使用真实 Harness 运行时、Cordis 和 MCP Client 验证自动注册 31 个工具、侧栏请求交接、重复命名空间冲突、停用清理、重新启用及自定义名称。验证安装与卸载、无效原生程序拒绝、打包协议校验，以及构建后的侧栏客户端生命周期；Codex 路由回归通过。本次合并验证未连接远端，其他平台仍需 CI 与实机验收。

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

以下 31 个工具面向 Agent，定义见 [src/server.ts](src/server.ts)。远端能力可通过会话状态中的 `capabilities` 查询，窗口与元素操作的具体平台能力通过 `crossdesk_desktop({session_id, operation: "capabilities"})` 查询。

| 工具 | 作用 |
| --- | --- |
| `crossdesk_open` | 打开设备选择/连接面板，返回 `view_id` |
| `crossdesk_open_browser` | 用户显式请求的浏览器入口/回退 |
| `crossdesk_list_devices` | 本机设备簿和授权摘要，不提供云发现 |
| `crossdesk_connect` | 通过别名连接已经授权且有凭据的设备 |
| `crossdesk_get_session` | `session_id`、`view_id` 严格二选一；查询状态或等待连接、恢复、指定解锁请求，`wait_ms` 范围 0–25000 |
| `crossdesk_list_sessions` | 列出活动会话及设备、显示器和控制状态，最多八个并行连接 |
| `crossdesk_set_recovery` | 开关有界断线恢复，尝试次数可设为 1–5，默认 3；需要持久授权和可用凭据 |
| `crossdesk_disconnect` | 断开并释放输入状态、子进程和设备锁 |
| `crossdesk_screenshot` | 返回 PNG image content 和观测元数据 |
| `crossdesk_wait` | 根据已有观测等待画面区域变化或稳定，满足条件后返回新截图 |
| `crossdesk_action` | 结构化输入，可附带动作之后的截图 |
| `crossdesk_release_inputs` | 释放当前会话持有的所有按键和鼠标按钮，并取消当前输入动作 |
| `crossdesk_set_control` | 在已授权范围内启停 Agent 输入 |
| `crossdesk_select_display` | 切换显示器，使旧观测失效 |
| `crossdesk_capture_displays` | 顺序截取最多八个显示器，返回多屏图像及 `capture_id`，随后恢复原显示器 |
| `crossdesk_drag_across` | 使用多屏截图坐标跨显示器拖拽 |
| `crossdesk_video_settings` | 运行时调整预览帧率、分辨率、质量和码率 |
| `crossdesk_desktop` | 查询平台能力、窗口清单或可访问性树，支持按进程和元素读取子树 |
| `crossdesk_desktop_action` | 激活窗口、按下/聚焦元素、设置值、启动应用或打开 HTTP(S) 地址 |
| `crossdesk_wait_ui` | 按文本和角色等待窗口或可访问性元素出现、消失 |
| `crossdesk_audio` | 开关远端桌面音频接收，默认关闭 |
| `crossdesk_audio_capture` | 采集 100–10000 毫秒远端音频，返回单声道 48 kHz WAV 音频内容块 |
| `crossdesk_clipboard_get` | 读取远端文本剪贴板，需要已授权控制会话 |
| `crossdesk_clipboard_set` | 写入或清空远端文本剪贴板 |
| `crossdesk_files_list` | 分页浏览远端目录或文件系统根目录 |
| `crossdesk_files_transfer` | 上传或下载文件、文件夹，返回可查询的传输任务 |
| `crossdesk_files_status` | 查询传输进度、逐文件结果及完成/失败/取消状态 |
| `crossdesk_files_cancel` | 取消传输并清理未完成的暂存文件，保留已完成文件 |
| `crossdesk_system_status` | 查询远端系统锁定状态及安全桌面服务信息 |
| `crossdesk_system_command` | 锁定远端系统，或通过 Windows 服务发送安全注意序列 `send_sas` |
| `crossdesk_unlock` | 使用独立的系统锁屏凭据解锁，或在本地面板请求输入密码 |

## MCP Apps 面板专用接口

以下 11 个接口仅在 `--panel=apps` 且当前请求支持 MCP Apps 时暴露为 app-only 工具，由经过视图令牌验证的面板调用。浏览器和终端面板通过各自经过认证的本地通道使用会话功能。

| 接口 | 作用 |
| --- | --- |
| `crossdesk_ui_ready` | 回报面板已初始化 |
| `crossdesk_ui_list_devices` | 获取设备簿及默认服务器配置 |
| `crossdesk_ui_get_session` | 查询当前面板绑定的会话 |
| `crossdesk_ui_connect` | 提交用户选择的设备、连接凭据及授权选项 |
| `crossdesk_video_open` | 获取当前视图的实时视频连接票据 |
| `crossdesk_ui_preview` | 实时视频受限时获取当前视图的有界 PNG 预览，仅通过面板元数据返回 |
| `crossdesk_viewer_control` | 人工暂停、恢复、接管或断开 |
| `crossdesk_ui_input` | 人工接管期间发送面板键鼠输入 |
| `crossdesk_ui_unlock` | 提交或取消锁屏密码输入，可选择保存凭据 |
| `crossdesk_ui_recovery` | 设置断线恢复开关和尝试次数 |
| `crossdesk_ui_audio` | 开关面板会话的音频接收 |

## 远程操作

### 键鼠输入

`crossdesk_action` 支持 `move`、`click`、`double_click`、`scroll`、`drag`、`key_press`、`hotkey`、`type_text`，以及可跨动作保持的 `key_down`、`key_up`、`mouse_down`、`mouse_up`。按键使用 Windows VK 对应的标准名称，覆盖 `A`–`Z`、`0`–`9`、`F1`–`F24`、数字键盘、`CTRL`、`ALT`、`SHIFT`、`META`、`ENTER`、`ESC`、方向键、菜单、锁定键和媒体键等；应用快捷键及实际效果仍需根据远端系统选择。

`move`、`click`、`double_click`、`scroll`、`drag` 和 `key_press` 支持可选的 `modifiers: ["CTRL", "ALT", "SHIFT", "META"]`（选择需要的键，不能重复），用于组合点击、选择、拖拽和快捷键。修饰键仅在本次动作期间保持，结束、取消或人工暂停时释放。`drag` 可指定 `button: "left" | "right" | "middle"`，默认左键。标点键支持 `SEMICOLON`、`EQUAL`、`COMMA`、`MINUS`、`PERIOD`、`SLASH`、`BACKQUOTE`、`BRACKET_LEFT`、`BACKSLASH`、`BRACKET_RIGHT`、`QUOTE`；这些是 VK 键位名称，实际字符取决于远端键盘布局，输入文字应使用 `type_text`。例如：

```json
{"session_id":"…","operation_id":"select-item-1","observation_id":"…","action":{"type":"click","x":120,"y":80,"modifiers":["SHIFT"]}}
```

`key_down`、`key_up`、`mouse_down` 和 `mouse_up` 的按下状态可跨动作保持，例如按住 Ctrl 后移动或拖动鼠标。最后一次动作完成后，按住状态的有效期为十五秒，超时自动释放。`crossdesk_release_inputs` 可立即释放本会话的全部按键和按钮，并取消当前输入动作。人工暂停、断开及输入错误同样释放按住状态。

`key_press`、`hotkey` 支持 `duration_ms`，最长三十秒；`drag` 时长上限同样为三十秒。状态返回 `held_key_codes` 和 `held_mouse_flags`。文本输入前应释放持续按住的键鼠状态。

### 减少 Agent 操作往返

`crossdesk_action` 支持 `action.type: "sequence"`，在一次调用中依次执行 1–8 个固定动作，最后只返回一次截图。例如已聚焦远程浏览器时，可以将聚焦地址栏、输入网址、回车合并：

```json
{
  "session_id": "current_session",
  "operation_id": "open_bilibili",
  "observation_id": "current_observation",
  "action": {
    "type": "sequence",
    "actions": [
      { "type": "hotkey", "keys": ["META", "L"] },
      { "type": "type_text", "text": "https://www.bilibili.com" },
      { "type": "key_press", "key": "ENTER" }
    ]
  },
  "wait": { "condition": "changed", "timeout_ms": 1500 }
}
```

该例适用于 macOS；其他系统按实际快捷键选择修饰键。只有无需中途观察判断的动作才能合并，所有坐标仍对应起始观测。序列禁止嵌套和跨动作保持按下状态，累计文本不超过 16 KiB，计划输入时长（含间隔）不超过 8 秒，执行总期限为 25 秒。`interval_ms` 为动作间最小间隔，默认 100 毫秒，可设为 0–500；它不代表应用完成加载。原生层在首次输入前验证整组动作，每步等待支持的输入回执；回执失败、超时、人工暂停、取消或几何变化都会停止余下动作。旧被控端没有回执时保留 `remote_execution: "unsupported"`，最终画面仍需检查。`sequence.finished_actions` 表示已结束输入阶段的动作数，错误时的 `stopped_at` 是从 0 开始的停止位置，不代表应用业务结果。

连接和状态结果的 `automation` 汇总本机序列支持、远端输入回执及桌面自动化能力。Agent 应复用当前会话和有效观测，支持桌面自动化时优先用 `crossdesk_desktop_action` 直接打开网址、应用或操作可访问性元素；未声明支持时直接使用截图输入路径。旧 CLI 不支持序列时在任何输入前返回 `NATIVE_SEQUENCE_UNSUPPORTED`，需更新本机 CLI。远端能力未声明时会给出兼容性提示，不自动断开连接或尝试不支持的操作。

动作结果的 `timing_ms` 包含原生输入阶段、MCP 到 CLI 的输入往返、等待及截图阶段和工具内总耗时，可用来区分工具执行与工具调用之外的宿主、模型或语音延迟。

### 截图与条件等待

截图默认最长边 1280，最大 2048；允许指定源帧像素区域。返回 `source_size`、`source_rect`、`image_size`、`generation`、`geometry_epoch`、`frame_id`、`frame_age_ms` 和 `observation_id`。动作坐标使用返回图像坐标，不使用浏览器 CSS 像素。观测保留最多 32 个，30 秒过期；切换显示器、连接代际或图像几何变化立即失效。截图使用不超过 2 秒的帧，PNG 不超过 2 MiB，必要时继续缩小输出并返回真实尺寸。

`timeout_ms` 限制等待可用帧的时间，PNG 编码独立执行；`0` 表示只使用当前已就绪的新鲜帧，无帧返回 `FRAME_TIMEOUT`，编码器繁忙返回 `BUSY`。截图按到达顺序处理，并优先于预览。视频超过 2 秒未更新时拒绝新的键鼠输入，恢复新鲜画面后可继续；已提交操作的去重查询仍可读取原结果。

键鼠动作的 `operation_id` 在当前原生会话中缓存最近 128 个操作，至少在未被容量淘汰的 5 分钟窗口内支持同参数查询；不同参数复用 ID 返回冲突，整组序列同样受此规则约束。`submitted` 表示输入已发送，远端原生输入处理状态见 `remote_execution`，应用界面的实际结果仍需后续观察确认。超时、断网、取消或发送失败可能返回 `outcome_unknown`/`side_effect_possible`，禁止自动重放。

`observe_after` 默认开启：在同一次调用内最多等待 1000 毫秒的画面变化，再返回最终截图及 `wait.matched` / `wait.wait_timed_out`。纯移动和单独按下／释放默认只取新帧，避免等待本来就不会变化的像素；仍可显式指定 `wait`。显式 `wait` 可选择 `changed`、`stable`，或指定 `condition: "ui"` 与 `text` / `role` 等待远端可访问性元素出现；最长等待 5000 毫秒。视觉等待可以提供同一会话的预先裁剪观测 `wait.observation_id`，避免视频、动画或时钟等无关区域干扰。新版 CLI 的视觉等待只接受动作结束帧之后的帧，旧 CLI 返回 `wait.frame_bound: false`。等待超时或截图错误不会覆盖已经提交的输入结果，也不会重放动作；变化或稳定不等于应用任务成功。`observe_after: false` 只返回输入结果，不能同时指定 `wait`。

更新后的被控端通过 `capabilities.input_ack` 声明输入处理回执。动作结果的 `remote_execution` 表示远端原生输入处理状态，应用界面的实际结果由后续观察确认。旧被控端返回 `unsupported`。回执等待有明确期限，超时动作保留结果不确定状态。

`crossdesk_wait` 使用已有截图的 `observation_id`，等待对应区域变化（`changed`）或持续稳定（`stable`）。可设置 `stable_ms` 和最长 25000 毫秒的 `timeout_ms`。满足条件后返回新截图，切换显示器、断开和取消会结束等待。

### 锁屏解锁与密码保存

Agent 识别到远端系统锁屏后，先聚焦画面中的系统密码框并重新截图，再调用 `crossdesk_unlock`，传入 `session_id`、`operation_id` 和截图的 `observation_id`。默认 `use_saved: true` 会读取该设备独立保存的锁屏凭据。缺少凭据时，浏览器或 Apps 面板通过悬浮弹窗显示隐藏输入框，终端观看器显示隐藏输入提示。弹窗可关闭，并通过左侧连接信息中的“输入密码”重新打开。提交结果显示为六秒后收起的悬浮提示，连接信息持续展示解锁状态和锁屏凭据配置状态。

用户可在解锁输入框勾选“保存密码并允许以后自动解锁”。连接表单的“保存与授权”和本机 `crossdesk-mcp setup` 也提供锁屏密码配置。持久保存要求设备已在本地设备簿中，系统凭据库使用独立账户保存访问密码和锁屏密码。设备列表返回 `unlock_credential_provider` 摘要，配置文件保存 `unlock_credential` 引用。环境变量方式使用 `unlock_credential: "environment"` 和 `unlock_environment_variable`，旧设备配置继续有效。

等待输入时，Agent 使用 `crossdesk_get_session`，提供 `session_id` 或 `view_id`、`unlock_request_id` 和 `wait_ms: 25000`。`wait_timed_out` 后可继续等待，等待取消仅结束当前查询。解锁请求有效期为五分钟，等待期间保留实时画面，常规 Agent 输入和显示器切换会返回 `UNLOCK_PENDING`。用户取消、人工暂停、连接结束或画面代际变化会终止请求。

提交前重新获取同一显示器代际的截图，原生端执行选择密码框内容、文本输入和回车。对支持 `capabilities.system` 的被控端，服务在发送密码前先查询锁定状态，已经解锁时返回 `unlock.state: "verified"` 和 `unlock.result.status: "already_unlocked"`，不再输入密码。提交后会进入 `verifying`，在八秒验证窗口内查询系统状态；确认解锁时返回 `verified`，无法确认时保留 `submitted` 和验证信息。旧被控端成功发送后返回 `submitted`。Agent 随后获取新截图，确认桌面状态后继续原任务。

用户在解锁弹窗选择保存密码时，支持系统状态的被控端仅在验证为已解锁后写入凭据库；未确认时返回 `persistence_error: "UNLOCK_NOT_CONFIRMED"`。旧被控端仍在成功发送后保存，密码正确性需通过画面确认。密码被拒绝时，使用新请求并设置 `use_saved: false` 让用户更正，结果不确定时先观察画面。相同 `operation_id` 与参数查询原请求结果。

锁屏密码通过经过认证的本地面板或终端 IPC 进入原生输入通道。模型侧工具参数和结果只包含请求状态，原生操作缓存仅保存解锁请求标识及结果。解锁遵守现有控制授权和人工暂停状态，依赖远端文本输入能力。`off` 模式可使用预先配置的锁屏凭据，缺少凭据时返回 `UNLOCK_CREDENTIAL_REQUIRED`。原生 CLI 通过 `unlock_protocol: "password/1"` 声明支持，更新源码后需重建 CLI 和 MCP 并重新启动服务。

### 远端剪贴板

`crossdesk_clipboard_get` 读取远端文本剪贴板，`crossdesk_clipboard_set` 设置文本，空字符串清空。读取上限为 128 KiB，单次工具写入上限为 16 KiB UTF-8。写入需要 `operation_id`，相同请求查询原结果。工具使用已授权控制会话，并遵守人工暂停及解锁等待状态。更新后的被控端通过 `capabilities.clipboard` 声明支持，写入成功由系统剪贴板更新回执确认。

### 文件浏览与传输

`crossdesk_files_list` 浏览远端目录，空路径选择远端主目录，`@roots` 返回文件系统根目录。每页最多 256 项，使用 `request_id` 和 `next_offset` 继续读取。超时返回可继续查询的请求标识。

`crossdesk_files_transfer` 支持上传和下载文件、文件夹，源路径为绝对路径，目标目录需要已存在。传输使用 `operation_id` 去重，通过 `crossdesk_files_status` 查询每个文件的字节进度和终态，通过 `crossdesk_files_cancel` 取消并清理未完成文件。每个连接同时处理一个传输任务，多文件按现有传输引擎排队。已完成任务保留四份历史记录，状态中的文件清单分页返回。目标同名文件按现有冲突命名规则另存。

目录浏览及新建传输需要已授权且未暂停的控制会话，并要求被控端声明 `capabilities.files`。下载请求绑定目标目录及传输标识，接收器接受已为该请求预留的文件。人工暂停后仍可查询或取消已有任务。

### 系统状态与安全桌面

`crossdesk_system_status` 查询系统锁定状态。Windows 同时报告服务可用性和交互桌面阶段。`crossdesk_system_command` 支持 `lock`，Windows 可通过已运行的服务执行 `send_sas`。命令需要控制授权和独立的 `operation_id`，相同请求查询原结果。系统接受命令后，通过状态及画面观察确认实际界面。

### 多会话与显示管理

同一 MCP 实例支持最多八个并行设备连接。`crossdesk_list_sessions` 返回各连接的设备、显示器及控制状态。面板按设备绑定会话，断开一个连接会释放该连接的资源。每个连接的诊断日志位于 `logs/<session_id>/transport.log`。

`crossdesk_capture_displays` 按顺序截取最多八个显示器并恢复原显示器，通过一次工具结果展示多屏画面。`capture_id` 有效期三十秒，供 `crossdesk_drag_across` 使用各屏截图坐标完成跨屏拖拽。拖拽保持按下状态跨越显示器切换，在完成、取消或异常时释放按钮。普通输入操作使用重新选择显示器后取得的新截图。

`crossdesk_video_settings` 在运行时调整预览帧率、分辨率、质量和码率，返回远端接受状态。设置受对端能力限制。多屏采集及跨屏拖拽期间，同一会话的其他输入请求返回忙碌状态。

### 有界断线恢复

`crossdesk_set_recovery` 和面板的“断线自动恢复”开关用于启用恢复。默认关闭，启用要求设备已保存连接凭据及持久授权。后台监测覆盖临时信令中断、连接丢失和原生进程异常，使用退避策略，在六十秒预算内最多执行配置次数的重连。每次重连重新核对设备身份和授权，人工暂停、关闭恢复开关和主动断开会停止恢复。

恢复期间返回 `reconnecting`，可以用 `crossdesk_get_session` 携带 `session_id` 和 `wait_ms` 等待。恢复成功后返回新的 `session_id`，既有监看面板跟随新连接。旧会话标识可用于查询替代连接，后续操作使用新会话及新截图。截图标识包含所属会话，避免不同原生进程的观测编号相互混淆。先前的输入、解锁和文件任务保持原有结果边界，恢复流程只重建连接并恢复观看配置。

### 面板人工接管

浏览器及 Apps 面板提供“人工接管”和“交还 Agent”。接管后面板可操作鼠标、滚轮、键盘快捷键和中文输入法，连接信息展示当前控制权。输入绑定面板、控制批次和画面尺寸，交接会释放按住状态并使旧批次失效。画面超过两秒未更新时停止发送新输入，窗口失焦或隐藏时释放按键。原生层保留十五秒的输入租约作为断线清理保障。

Agent 在人工接管期间仍可观察画面，键鼠、显示设置和其他要求控制权的操作等待用户在面板交还控制。连接需预先授予控制权限，并使用支持 `events/1` 的原生 CLI。

### 远端声音

`crossdesk_audio` 开关远端桌面音频接收，默认关闭。面板的“播放声音”按钮同时启用接收和本地播放，音量在本面板调节。播放使用已授权的视频 WebSocket 通道，PCM 队列与浏览器播放时延均设有上限，拥塞时丢弃过时音频。浏览器首次播放需要用户点击按钮。

`crossdesk_audio_capture` 采集接下来 100 至 10000 毫秒的音频，通过 MCP 音频内容块返回单声道 48 kHz WAV。采集期间临时开启接收，结束或取消后恢复之前的设置，未收到足够音频时返回 `AUDIO_UNAVAILABLE`。系统需允许被控端捕获桌面声音。自动恢复连接会恢复用户选择的音频接收设置。

### 窗口、应用与可访问性元素

`crossdesk_desktop` 查询平台能力、窗口清单及可访问性树。可按进程或先前快照中的元素继续读取子树，节点包含角色、名称、可读取值和系统原生桌面坐标。每份快照最多 128 个节点，响应限制为 48 KiB 左右，超出范围返回 `truncated`。元素引用绑定连接，有效期三十秒，最多保留八份快照。

`crossdesk_desktop_action` 支持激活窗口、按下或聚焦元素、设置文本值、启动应用以及打开 HTTP(S) 地址。元素操作使用快照和 `element_id`，执行后该快照失效。所有修改操作需要控制授权及 `operation_id`，相同请求查询已有结果。安全密码字段隐藏值并拒绝通过结构化接口修改，系统解锁使用本地密码流程。操作结果表示系统接口接受情况，随后使用画面或新树确认界面变化。

`crossdesk_wait_ui` 根据文本及角色等待元素出现或消失。读取范围不完整时无法确认消失，会在超时结果中返回 `search_incomplete`。

macOS 使用 AX，需要系统辅助功能权限。Windows 使用 UI Automation，窗口激活遵守系统前台窗口及权限约束。Linux 构建时若检测到 `atspi-2` 开发包，会启用 AT-SPI 元素访问，否则提供 X11 窗口查询和激活。Wayland 的元素访问依赖桌面和应用提供 AT-SPI。应用启动参数接受 macOS bundle ID 或绝对 `.app` 路径、Windows 绝对 `.exe` 路径、Linux 绝对可执行文件路径。Linux 接口的所有权和超时处理依据官方文档。[AT-SPI 接口文档](https://docs.gtk.org/atspi2/)

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

修改后重启 MCP，再建立连接。选项适用于整个 MCP 进程，两条路径均将解码选项传给原生端；浏览器路径中的原生解码用于截图缓存。PNG 编码按截图请求触发，包括单屏截图、多屏采集及动作或等待后的截图，WebSocket 实时视频不触发 PNG 编码；Apps 的兼容预览按需触发 PNG 编码。这里配置的是控制端解码器，不更改远端编码器。原有配置文件无需新增字段即可继续使用。

Agent 模式初始请求高帧率档位，初始分辨率上限为 1920 × 1080。连接后可通过 `crossdesk_video_settings` 调整帧率、分辨率、质量和码率；工具接受的分辨率最大为 3840 × 2160，实际分辨率和帧率由远端能力、网络和本机解码性能决定。面板显示实际尺寸与呈现 FPS。浏览器路径需要 WebCodecs H.264 解码能力；MiniRTC 路径仅需浏览器支持从 NV12 创建 VideoFrame 并绘制。浏览器面板能力不足时明确报错；Apps 面板无法连接实时通道或不支持解码时切换到下述有界 PNG 预览。NV12 不压缩，1080p / 30 FPS 约需 93 MB/s 本机传输，4K / 30 FPS 约需 373 MB/s；它不增加远端到控制端的 H.264 网络码率。移除 Node 转发没有消除 NV12 自身的本机传输与浏览器像素处理开销；这不是 GPU 零拷贝。

视频 WebSocket 在原生 CLI 的独立 I/O 线程中运行，只监听随机 `127.0.0.1` 端口，与 Node 面板 HTTP 端口不同。浏览器面板通过 cookie/CSRF 授权获取视频票据，Apps 通过 app-only `crossdesk_video_open` 和视图令牌获取。票据 15 秒内一次性有效，绑定视图、会话和代际，通过 WebSocket 第一条消息提交，不写入 URL、日志或模型可见工具结果。WebSocket 提供画面、已启用的 PCM 音频、收帧 ACK 和关键帧恢复请求，不能执行远端输入操作。最多 8 个活动观看者。H.264 丢帧后等待关键帧恢复；NV12 每帧独立，每个观看者最多一帧未确认，原生发送队列只保留最新画面；H.264 最多 8 帧未确认并受字节阈值限制。10 秒未确认最早一帧则关闭该视频通道，不阻塞其他观看者或 MCP 控制。视图撤销、显示器/连接代际变化、断开和进程退出会关闭对应视频连接；暂停 Agent 控制仍继续播放。页面隐藏或卸载时释放视频订阅，恢复可见时重新订阅。

浏览器页面和 MCP Apps 资源的 CSP 允许原生服务使用的 `ws://127.0.0.1:*`（仅动态端口），HTTP 控制仍限制到 Node 面板的确切地址。原生服务另外校验确切 Host、Origin 和一次性票据。宿主仍可能限制 loopback、WebSocket、WebCodecs 或 iframe 权限。已观察到 Codex 内嵌宿主过滤 HTTP/WS 白名单，最终以 `connect-src 'self'` 拦截原生视频连接；添加同义 CSP 字段无法解决协议限制。播放器区分 CSP 拦截，并在 Apps 实时视频失败时通过已授权的 MCP 通道提供兼容预览。

兼容预览显示“预览”和实际 FPS，最高每秒 1 帧、最长边 960 像素、PNG 每帧不超过 256 KiB，不含声音。`crossdesk_ui_preview` 仅供面板使用，验证视图令牌、代际及绑定会话，每个视图最多一个进行中的请求，并在响应前重新检查会话是否改变。图像只放在工具结果 `_meta` 中，不进入模型可见内容，也不占用 Agent 的操作截图历史。前端直接解码 PNG 字节，无需新增网络白名单；隐藏或卸载面板时停止刷新，迟到响应不再绘制。需要流畅视频及声音时使用 `--panel=browser`；`--panel=apps` 仍禁止自动打开外部浏览器。

2026-11-11 在本机 Codex MCP Apps 中以仅查看模式连接 macOS 远端，确认 WebSocket 被 CSP 拦截后自动显示兼容预览，画布帧计数持续递增，实测约 0.5–1 FPS。该画面在 256 KiB 上限内自适应缩小为 540 × 303；此结果验证了内嵌预览，不代表宿主已支持本机 WebSocket 实时视频。

## 打包

```sh
npm run build
npm run pack:npm -- --out-dir /absolute/path/artifacts
```

默认生成通用 tarball，包含 MCP、DSH 插件入口与 bundle 声明、面板、源代码、许可和固定版本的五个平台可选依赖。CI 的 `MCP package` 工作流上传此通用 `mcp-package`，不发布 npm。使用通用包前需要对应版本的原生 npm 依赖已经可用。

为 DSH 生成只需安装一次的平台包时，将 CLI 工作流生成的原生 `.tgz` 解压到工程外目录，再传入其 `package` 目录：

```sh
native_package_dir=$(mktemp -d)
tar -xzf /absolute/path/chunkdash-crossdesk-cli-darwin-arm64-0.2.0.tgz -C "$native_package_dir"
npm run pack:npm -- --native-package-dir "$native_package_dir/package" --out-dir /absolute/path/platform-artifacts
rm -rf "$native_package_dir"
```

平台包保留原生包的程序、源码、许可和构建信息，设置对应 OS / CPU / libc 限制，不再下载原生平台依赖；其他 JavaScript 依赖仍由包管理器安装。CLI 构建工作流为每个平台生成 `crossdesk-mcp-<平台>` 产物，CLI、MCP、DSH 和原生依赖版本不一致时直接停止构建或打包。通用包和平台包名称、版本相同，应放在不同输出目录，不要将平台包当成通用版本发布到 npm。

原生 `doctor` 必须同时声明自动化协议和 `automation_video_protocols: ["h264-annexb/1", "nv12/1"]`、`automation_video_decoders: ["auto", "software"]`，以及 `automation_video_transport: "loopback-websocket/1"`、`automation_terminal_protocol: "ansi/1"`。通用 npm 分发需要先发布对应 0.2.0 平台包，再发布 MCP；内含原生程序的平台包不受此顺序限制。构建通过不等于各宿主和设备上的联调完成。

从发布 tarball 重建 TypeScript/面板时，将包中的 `build-package.json` 复制为 `package.json`，运行 `npm ci && npm run build`。原生源码和依赖来源随平台包提供，详见 [源码来源](../cli/docs/source-provenance.md)。

## 本地审阅验证（2026-11-03）

动作参数校验和输入步骤生成集中在 `apps/cli/automation_protocol.*`，主循环负责调度、取消和权限检查。截图按色度网格对齐后仅转换所需区域，PNG 在容量受限的缓冲区中单次压缩；原生帧仅在没有其他读取者时复用内存。Node 按行累计响应字节，在完整响应到达时解码，避免反复扫描和复制大截图。

本机 macOS arm64 完成 TypeScript/面板构建、原生 CLI 构建、现有 wire/keyboard-state 测试，以及工程外的 MCP、会话和原生动作回归检查。ASan/UBSan 检查覆盖动作调度和输入清理；140 组随机图像覆盖完整帧、奇偶裁剪边界、缩放及容量回退，优化前后 PNG 字节一致。

使用 1920 × 1080 合成 NV12、最长边 1280、每项 6 次的 `-O2` 本地基准：完整画面编码约 22.5 → 11.5 ms，321 × 241 裁剪约 2.1 → 0.9 ms。数字仅衡量图像转换与编码，不包含网络、解码、远端执行或宿主渲染；未连接真实远端或完成 Windows/Linux 验收。额外测试代码、脚本、数据和产物均存放于工程外系统临时目录。
