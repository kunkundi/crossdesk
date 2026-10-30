# CrossDesk CLI

CrossDesk 的终端主控客户端，源码位于本仓库的 `apps/cli`。通过真实 MiniRTC ID 会话观看远程桌面；默认只读、静音。包含 ANSI 象限字符画面、局部放大和平移、Kitty 图形后端，以及 Pi 自定义 TUI 扩展。

## 画面预览

| ANSI 字符画面 | Kitty 协议像素画面 |
| --- | --- |
| <img src="../../docs/images/cli-ansi-overview.png" width="480" alt="CrossDesk CLI 的 ANSI 全景输出，包含只读状态栏与快捷键提示" /> | <img src="../../docs/images/cli-kitty-overview.png" width="480" alt="CrossDesk CLI 的 Kitty 像素全景输出，显示同一远程桌面" /> |

图示来自 **2026-10-31** 的实际只读连接，通过伪终端采集并渲染协议输出；来源与边界见 [图示说明](../../docs/images/README.md#cli-captures)。默认 `auto` 探测 Kitty 支持，失败时使用 ANSI；普通终端可指定 `--renderer ansi`，Ghostty 等支持 Kitty 图形协议的终端可指定 `--renderer kitty`。ANSI 全景适合查看桌面轮廓，阅读小字需进入细节模式。

## 安装与连接

### 通过 npm 安装

需要 **Node.js 22 或以上**和 npm。统一入口为 [@chunkdash/crossdesk-cli](https://www.npmjs.com/package/@chunkdash/crossdesk-cli)，分发目标为 macOS / Linux（x64、arm64）和 Windows（x64）。安装时自动选择对应的原生程序，无需 Xmake 或 C++ 编译工具链：

```sh
npm install -g @chunkdash/crossdesk-cli@latest
crossdesk-cli doctor --json
crossdesk-cli 123456789
```

将示例 ID 替换为被控电脑的 9 位 ID，按提示隐藏输入密码。`doctor` 仅检查本机环境，不发起连接。更新到最新版时重新执行上述安装命令。

无需全局安装时，使用 `npx`，首次运行会下载对应平台包：

```sh
npx @chunkdash/crossdesk-cli@latest doctor --json
npx @chunkdash/crossdesk-cli@latest 123456789
```

安装后命令为 `crossdesk-cli`。五个平台依赖使用 `@chunkdash/crossdesk-cli-*`，由启动器自动选择；请保留 npm 的可选依赖，不要使用 `--omit=optional`。旧的无 scope 平台包不属于这套依赖，仅安装平台包不会注册命令。三端实机验收尚未全部完成；运行要求、旧包迁移、打包与发布步骤见 [npm 分发](docs/npm-distribution.md)。

### 从源码构建

在 CrossDesk 仓库根目录执行以下命令构建 CLI：

```sh
./apps/cli/scripts/prepare-deps.sh
xmake f -y --CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y --USE_CUDA=n -m release
xmake build crossdesk-cli
xmake run crossdesk-cli doctor --json
xmake run crossdesk-cli 123456789 --quality high --zoom native
```

构建后可直接运行 `build/<platform>/<arch>/release/crossdesk-cli`，也可将对应目录加入 PATH；Windows 的文件名为 `crossdesk-cli.exe`。

### 常用连接命令

以下示例适用于 npm 全局安装，或已将原生程序加入 PATH 的环境。`crossdesk-cli <设备ID> [选项]` 为默认连接方式，`view <设备ID>` 保留为兼容别名。设备 ID 是格式示例，使用时替换为你有权访问的设备，密码通过隐藏提示输入：

```sh
crossdesk-cli 123456789 --renderer kitty
crossdesk-cli 123456789 --renderer ansi --zoom native
crossdesk-cli 123456789 --quality balanced --fps 8
crossdesk-cli 123456789 --display 1
```

默认请求 `high` 画质（最高 1920 × 1080），`--display` 的索引从 `0` 开始。交互 CLI 默认本地输出目标为 30 FPS，远端供帧会随输出耗时自适应；显式 `--fps` 固定目标和相应预览请求，实际帧率仍取决于设备、终端与链路。更多参数见 [主 README](../../README.md#cli) 和 [终端使用指南](docs/terminal-client.md)。

## 查看、控制与退出

| 操作 | 按键 |
| --- | --- |
| 开启远端控制 | 只读模式下按 `c` |
| 放大局部 / 回到全景 | 只读或本地命令模式下按 `d` / `f`；只读时也可点击目标进入细节 |
| 缩放 / 平移 | 只读或本地命令模式下用 `+/-` 或滚轮缩放；方向键或 `i` 上、`j` 左、`k` 下、`l` 右平移 |
| 暂停远端输入并进入本地命令模式 | `Ctrl+]` |
| 选择返回后的控制状态 | 本地命令模式下按 `c`，再按 `Esc` / `Ctrl+C` 返回 |
| 退出连接 | 只读模式下按 `q` / `Ctrl+C`；控制模式下先按 `Ctrl+]`，再按 `q` |

本地命令模式在缩放、平移和按 `c` 后持续保持，直到手动返回。控制模式中的 `Ctrl+T`、`Ctrl+C` 和普通按键均传给远端。再次按 `Ctrl+]` 也可离开本地命令模式，若返回控制模式则同时向远端发送该组合键。

## 更多文档与功能范围

- [构建、使用、Pi 安装和故障排查](docs/terminal-client.md)
- [私有桥接协议](docs/bridge-protocol.md)
- [实际验证记录](docs/validation.md)
- [源码来源和依赖修补](docs/source-provenance.md)

CLI 仅作为控制端，每次连接通过服务端获得新的 `cli-xxxxxxxxx` 临时 ID。使用前须部署支持 `cli` 注册的 crossdesk-server；旧服务端不会回退为普通设备身份。

终端 I/O 包含 POSIX（Linux/macOS）和 Windows Console / ConPTY 实现。macOS arm64 已完整构建，之前已连接真实被控端验证 ANSI 观看与预览协商。Linux / Windows 全量构建和真实连接、SSH/Kitty 实机显示及真实 Pi TUI 仍需验收；交叉编译检查不等于完整运行验证。此项目不创建被控端虚拟桌面，不提供音频播放、文件传输、剪贴板同步或 MCP 服务。
