# CrossDesk CLI

CrossDesk 的终端主控客户端，源码位于本仓库的 `apps/cli`。通过真实 MiniRTC ID 会话观看远程桌面；默认只读、静音。包含 ANSI 象限字符画面、局部放大和平移、Kitty 图形后端，以及 Pi 自定义 TUI 扩展。

分发目标为 macOS / Linux（x64、arm64）和 Windows（x64）的原生终端客户端。npm 包发布后可统一使用：

```sh
npx crossdesk-cli@latest doctor --json
npx crossdesk-cli@latest 123456789
```

**当前尚未发布到 npm，也尚未完成三端实机验收。** 已加入平台包生成、自动选包入口和五个平台的 CI 构建配置；运行要求、打包与本地安装见 [npm 分发](docs/npm-distribution.md)。

在 CrossDesk 仓库根目录执行以下命令构建 CLI：

```sh
./apps/cli/scripts/prepare-deps.sh
xmake f -y --CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y --USE_CUDA=n -m release
xmake build crossdesk-cli
xmake run crossdesk-cli doctor --json
xmake run crossdesk-cli 123456789 --quality high --zoom native
```

`crossdesk-cli <设备ID> [选项]` 为默认连接方式，`view <设备ID>` 保留为兼容别名。最后一个设备 ID 是格式示例，使用时替换为你有权访问的设备。密码通过隐藏提示输入。只读时 `c` 开启控制、`q` 或 `Ctrl+C` 退出；点击目标或按 `d` 进入原始细节模式，`+/-` 或滚轮缩放，方向键或 `i` 上、`j` 左、`k` 下、`l` 右平移，`f` 回到完整桌面。任何模式下先按 `Ctrl+]` 进入持续本地命令模式，再按 `c` 选择返回后的控制状态、`q` 退出或视图按键连续调整画面，`Esc` / `Ctrl+C` 手动返回。控制模式中的 `Ctrl+T`、`Ctrl+C` 和普通按键均传给远端。ANSI 全屏缩略图仍受字符网格限制，默认请求 high 画质，阅读小字建议使用细节模式；支持 Kitty 协议的终端可使用 `--renderer kitty` 获得像素画面。

- [构建、使用、Pi 安装和故障排查](docs/terminal-client.md)
- [私有桥接协议](docs/bridge-protocol.md)
- [实际验证记录](docs/validation.md)
- [源码来源和依赖修补](docs/source-provenance.md)

CLI 仅作为控制端，每次连接通过服务端获得新的 `cli-xxxxxxxxx` 临时 ID。使用前须部署支持 `cli` 注册的 crossdesk-server；旧服务端不会回退为普通设备身份。

终端 I/O 包含 POSIX（Linux/macOS）和 Windows Console / ConPTY 实现。macOS arm64 已完整构建，之前已连接真实被控端验证 ANSI 观看与预览协商。Linux / Windows 全量构建和真实连接、SSH/Kitty 实机显示及真实 Pi TUI 仍需验收；交叉编译检查不等于完整运行验证。此项目不创建被控端虚拟桌面，不提供音频播放、文件传输、剪贴板同步或 MCP 服务。
