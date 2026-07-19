# GUI 目录结构与职责说明

本文档说明 `src/gui` 的目录结构、核心类型、依赖方向和代码归属规则，用于避免 `Render`、`GuiApplication` 或 `GuiRuntime` 再次演变成职责混杂的大类。

## 目录结构

```text
src/gui/
├── render.h                         # 对外稳定入口 Render
├── render.cpp                       # 创建 GuiApplication 并转发 Run
├── application/                     # SDL/ImGui 应用外壳
│   ├── gui_application.h            # GuiApplication 声明
│   ├── gui_application.cpp          # 初始化、主循环和清理
│   ├── application_state.h          # 窗口、交互和 UI 状态
│   ├── sdl_event_dispatch.cpp       # SDL 窗口及应用事件分发
│   ├── sdl_events.cpp               # 键盘和鼠标事件转换
│   ├── window_lifecycle.cpp         # 原生窗口及 ImGui 上下文生命周期
│   └── window_rendering.cpp         # 三类窗口的渲染流程
├── runtime/                         # 非界面的 GUI 运行时
│   ├── gui_runtime.h/.cpp           # 运行时协调接口与公共实现
│   ├── connection_runtime.cpp       # 连接、在线探测、超时及会话清理
│   ├── windows_service_runtime.cpp  # Windows 服务和安全桌面集成
│   ├── mac_permission_runtime.mm    # macOS 权限检查和系统设置调用
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
├── views/                           # ImGui 视图实现
│   ├── panels/                      # 主窗口内嵌面板
│   ├── toolbars/                    # 标题栏、状态栏和控制栏
│   └── windows/                     # 独立窗口和模态对话框
├── platform/                        # 原生桌面平台适配
│   └── tray/                        # Windows、macOS 和 Linux 托盘
└── assets/                          # 字体、图标、布局和本地化资源
```

## 分层关系

```text
Render
  └── GuiApplication
        ├── SDL/ImGui 生命周期
        ├── views/panels / views/toolbars / views/windows
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

## 核心类型

### Render

`Render` 是应用其他部分可见的稳定入口，只负责：

- 管理 `GuiApplication` 生命周期；
- 将 `Run()` 转发给 `GuiApplication`；
- 隔离 GUI 内部类型，避免实现细节扩散到其他模块。

不要向 `Render` 添加窗口状态、连接状态或业务方法。

### GuiApplication

`GuiApplication` 是 SDL/ImGui 应用外壳，负责：

- 初始化日志、配置、SDL 和功能模块；
- 创建、销毁原生窗口及 ImGui 上下文；
- 执行主事件循环；
- 分发 SDL 事件；
- 调用面板、工具栏和窗口的绘制方法；
- 在程序退出时按顺序清理资源。

它不应实现连接协议、文件传输、剪贴板或设备控制细节。

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
| `application/application_state.h` | SDL 窗口、渲染上下文、交互标志和 UI 显示状态 |
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
  → 创建主窗口
  → GuiApplication::MainLoop
```

### 建立远端连接

```text
remote_peer_panel
  → GuiRuntime::ConnectTo
  → 在线状态探测
  → 创建或复用 RemoteSession
  → MiniRTC JoinConnection
  → PeerEventHandler 接收连接状态
  → 创建串流窗口并开始渲染
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
SDL quit / tray exit
  → GuiApplication::Cleanup
  → CloseAllRemoteSessions
  → 停止设备及后台任务
  → 销毁 Peer
  → 销毁 ImGui 上下文和 SDL 窗口
  → SDL_Quit
```

## 新代码归属规则

新增代码时按以下规则选择目录：

| 功能 | 目录 |
| --- | --- |
| SDL 初始化、事件循环、窗口生命周期 | `application/` |
| 连接、会话、Peer 回调和平台运行时 | `runtime/` |
| 视频、音频、鼠标、键盘设备生命周期 | `features/devices/` |
| 键盘协议和按键状态 | `features/input/` |
| 剪贴板同步 | `features/clipboard/` |
| 文件传输 | `features/file_transfer/` |
| 配置持久化 | `features/settings/` |
| 主窗口内嵌区域 | `views/panels/` |
| 标题栏、状态栏、控制栏 | `views/toolbars/` |
| 独立窗口和对话框 | `views/windows/` |
| 系统托盘 | `platform/tray/` |
| 字体、图标、布局和本地化数据 | `assets/` |
