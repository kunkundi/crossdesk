# GUI 目录结构与职责说明

本文档说明 `apps/desktop/src/gui` 的目录结构、核心类型、依赖方向和代码归属规则，用于避免 `Render`、`GuiApplication` 或 `GuiRuntime` 再次演变成职责混杂的大类。

## 目录结构

```text
apps/desktop/src/gui/
├── render.h                         # 对外稳定入口 Render
├── render.cpp                       # 创建 GuiApplication 并转发 Run
├── application/                     # Slint 应用外壳
│   ├── gui_application.h            # GuiApplication 声明
│   ├── gui_application.cpp          # 初始化、UI 回调、状态同步和清理
│   ├── application_state.h          # 窗口、交互和 UI 状态
│   ├── server_window_state.h        # 被控端窗口的控制端选择逻辑
│   └── window_geometry.h           # 窗口定位计算
├── rendering/                       # Slint 与原生视频渲染器的衔接
│   └── slint_video_presenter.h/.cpp # 视频纹理呈现与渲染通知
├── runtime/                         # 非界面的 GUI 运行时
│   ├── gui_runtime.h/.cpp           # 运行时协调接口与公共实现
│   ├── connection_runtime.cpp       # 连接、在线探测、超时及会话清理
│   ├── gui_state.h                  # ApplicationState 与 RuntimeState 组合点
│   ├── runtime_state.h              # 配置、连接、平台和通信状态
│   ├── remote_session.h             # 单个远端会话 RemoteSession
│   ├── device_presence_cache.h      # 设备在线状态缓存
│   ├── peer_event_handler.h/.cpp    # 信令和连接状态回调
│   ├── peer_media_callbacks.cpp     # 视频和音频回调
│   ├── peer_data_callbacks.cpp      # 控制、剪贴板和文件数据回调
│   └── remote_action_codec.h/.cpp   # RemoteAction 编解码
├── features/                        # 可独立演进的功能模块
│   ├── clipboard/                   # 本地与远端剪贴板同步
│   ├── devices/                     # 媒体及输入设备生命周期
│   ├── file_transfer/               # 文件队列、发送、确认和进度
│   ├── input/                       # 键盘状态、命令和超时处理
│   └── settings/                    # 配置缓存和最近连接别名
├── ui/                              # Slint 声明式界面
│   ├── crossdesk_ui.slint           # 界面组件导出入口
│   ├── common.slint                 # 共享控件、字体尺寸、颜色和图标字形
│   ├── main_window.slint            # 主窗口、面板、设置及对话框
│   ├── stream_window.slint          # 串流窗口、标签页和控制栏
│   ├── server_window.slint          # 被控端窗口
│   ├── ui_localization.h           # 本地化数据到 Slint 属性的映射
│   └── icons/                      # SVG 图标资源
└── assets/                          # 嵌入字体和本地化数据
```

平台私有 GUI 实现统一位于 `apps/desktop/src/platform/<os>/gui/`，包括托盘、macOS Metal 渲染与权限、Windows 服务运行时等；Windows 与 Linux 共用的 OpenGL 渲染器位于 `apps/desktop/src/platform/common/gui/`。

## 分层关系

```text
Render
  └── GuiApplication
        ├── Slint 窗口、事件循环和定时器
        ├── ui/ 声明式组件与回调
        ├── SlintVideoPresenter → 平台 VideoRenderer
        └── GuiRuntime
              ├── SessionDeviceManager
              ├── KeyboardController
              ├── ClipboardController
              ├── FileTransferManager
              ├── SettingsManager
              └── PeerEventHandler
```

依赖方向应保持从上向下：

```text
公开入口 → 应用层 → 运行时协调层 → 功能模块 → 底层库
                 ↘ 视图层
```

底层功能模块不应反向依赖具体面板、工具栏或窗口。

桌面端与 iOS Bridge 统一依赖静态库 `crossdesk_wire`。线上契约头位于 `libs/wire/include/`，包含控制消息、鼠标指针类型、显示流 ID、数据流名称和文件传输线格式。操作系统输入注入、剪贴板、文件读写以及界面状态仍由各自应用实现，`libs/wire/` 不得依赖桌面端或操作系统专用头文件。

## 核心类型

### Render

`Render` 是应用其他部分可见的稳定入口，只负责：

- 管理 `GuiApplication` 生命周期；
- 将 `Run()` 转发给 `GuiApplication`；
- 隔离 GUI 内部类型，避免实现细节扩散到其他模块。

不要向 `Render` 添加窗口状态、连接状态或业务方法。

### GuiApplication

`GuiApplication` 是 Slint 应用外壳，负责：

- 初始化日志、配置、SDL 和功能模块；
- 创建、销毁 Slint 主窗口、串流窗口和被控端窗口；
- 运行 Slint 事件循环，通过定时器执行 `Tick()` 和视频帧调度；
- 将 Slint 回调转换为连接、键盘和鼠标操作，并处理 SDL 退出及剪贴板事件；
- 将运行时状态同步到 Slint 属性和模型，并管理原生视频呈现；
- 在程序退出时按顺序清理资源。

它不应实现连接协议、文件传输、剪贴板或设备控制细节。

SDL 仍用于音频、显示器信息、剪贴板等平台能力。旧 ImGui 视图、窗口事件处理和专用资源已移除；`ui/common.slint` 中的 `ImGuiLineStyle`、`ImGuiFontStyle` 和 `ImGuiCheckBox` 是当前界面使用的 Slint 组件，名称表示沿用的视觉风格，不依赖 ImGui 库。`assets/fonts/fa_solid_900.h` 仍为 Slint 窗口提供 Font Awesome 图标字体。

### GuiRuntime

`GuiRuntime` 是 GUI 进程的非界面协调层，负责：

- 初始化 MiniRTC Peer 及回调参数；
- 协调连接、远端会话和在线状态；
- 持有各功能 Manager/Controller；
- 处理跨功能模块的调用顺序；
- 提供平台集成功能所需的运行时上下文。

`GuiRuntime` 应保持为协调器。新的独立功能应优先创建 Manager 或 Controller，不应直接继续堆积到 `GuiRuntime`。

### RemoteSession

`RemoteSession` 表示一个连接中或已连接的远端端点，其生命周期覆盖：

- MiniRTC Peer 和连接状态；
- 视频帧、纹理和渲染区域；
- 远端显示器和控制栏状态；
- 音频、鼠标和键盘控制状态；
- 文件传输状态；
- Windows 服务及安全桌面状态。

`remote_sessions_` 是按远端 ID 索引的会话表。会话查找、连接和清理统一由运行时处理。

## 状态划分

状态定义按生命周期和使用范围划分：

| 文件 | 状态范围 |
| --- | --- |
| `application/application_state.h` | 应用退出、交互、更新、窗口生命周期请求和共享 UI 状态 |
| `GuiApplication::SlintUi` | Slint 窗口句柄、定时器、视频呈现器、托盘和界面同步缓存 |
| `runtime/runtime_state.h` | 配置、Peer、连接表、在线探测和平台集成状态 |
| `runtime/remote_session.h` | 单个远端会话独占的连接、媒体、控制和文件状态 |
| `runtime/gui_state.h` | 仅组合 `ApplicationState` 与 `RuntimeState` |

添加状态前应先判断其生命周期：

- 只属于某个远端连接：放入 `RemoteSession`；
- 只属于窗口或 UI：放入 `ApplicationState` 对应子状态；
- 属于整个 GUI 运行期：放入 `RuntimeState` 对应子状态；
- 只属于某个功能模块：优先作为 Manager/Controller 的私有成员。

## 主要运行流程

### 启动

```text
Render::Run
  → GuiApplication::Run
  → 初始化路径、日志和配置
  → 初始化 SDL 和功能模块
  → GuiRuntime::CreateConnectionPeer
  → GuiApplication::InitializeUi 创建主窗口并绑定回调
  → 启动 Tick 定时器和视频帧调度
  → slint::run_event_loop
       → Tick 协调连接、处理后台事件并同步窗口状态
```

### 建立远端连接

```text
MainWindow::connect_requested
  → GuiApplication::ConnectFromUi
  → GuiRuntime::ConnectTo
  → 在线状态探测
  → 创建或复用 RemoteSession
  → MiniRTC JoinConnection
  → PeerEventHandler 接收连接状态
  → SyncStreamWindow 创建串流窗口并开始呈现视频
```

### 接收远端数据

```text
MiniRTC callback
  → PeerEventHandler
  ├── peer_media_callbacks：视频、音频
  ├── peer_data_callbacks：控制、文件、剪贴板
  └── peer_event_handler：信令、连接状态、网络统计
       → 对应 Manager/Controller
       → 更新 RemoteSession 或运行时状态
```

### 退出和清理

```text
Slint quit / SDL quit / tray exit
  → GuiApplication::Cleanup
  → 停止设备及后台任务
  → CloseAllRemoteSessions 并等待会话清理
  → 分离视频呈现器并释放 Slint 窗口和托盘
  → SDL_Quit
```

## 新代码归属规则

新增代码时按以下规则选择目录：

| 功能 | 目录 |
| --- | --- |
| Slint/SDL 初始化、UI 回调、状态同步和窗口生命周期 | `application/` |
| Slint 与原生视频渲染器的衔接 | `rendering/` |
| 连接、会话、Peer 回调和平台运行时 | `runtime/` |
| 视频、音频、鼠标、键盘设备生命周期 | `features/devices/` |
| 键盘状态与桌面输入处理 | `features/input/` |
| 线上控制消息和线格式 | `libs/wire/` |
| 剪贴板同步 | `features/clipboard/` |
| 文件传输 | `features/file_transfer/` |
| 配置持久化 | `features/settings/` |
| 主窗口内嵌区域、设置和对话框 | `ui/main_window.slint` |
| 串流标签页、控制栏和统计面板 | `ui/stream_window.slint` |
| 被控端窗口 | `ui/server_window.slint` |
| 共享控件、字体尺寸、颜色和图标字形 | `ui/common.slint` |
| 系统托盘 | `apps/desktop/src/platform/<os>/gui/tray/` |
| 嵌入字体和本地化数据 | `assets/` |
| SVG 图标资源 | `ui/icons/` |
