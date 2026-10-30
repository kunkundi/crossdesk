# CLI 验证记录

## 本地仓库整合（2026-10-30）

环境为 macOS arm64、Xcode SDK 27.0、Xmake 3.1.1、C++17，关闭 CUDA。CLI 源码整合到 CrossDesk 的 `apps/cli`，共享现有 wire 与 MiniRTC；未新增或修改工程内测试。

- CLI 单独配置和 release 构建通过，目标为 `crossdesk-cli`；`doctor --json` 返回有效 JSON，动态链接依赖仅为系统库及框架，无 Slint / SDL。
- 同时启用桌面与 CLI 时，debug 配置下 `crossdesk`、`crossdesk-cli` 均编译通过。
- 原有仓库结构、wire 和键盘状态测试通过，未修改测试源码。
- 工程外桥接检查通过：JSON 协议、版本校验、分片、EOF、消息上限、慢消费者、本地信令失败、凭据描述符和非 TTY 拒绝。
- 工程外 POSIX PTY 检查通过：隐藏密码、无 DISPLAY/WAYLAND_DISPLAY、原始模式恢复、Kitty 探测超时和 tmux 下明确拒绝 Kitty。
- macOS arm64 原生 npm 包和通用启动包生成成功；离线安装、启动器 `doctor` 和原生程序路径解析通过。该本地产物最低要求为 macOS 27.0。
- 从原生包解出源码后，在没有 Git 元数据、没有桌面应用源码的目录中重新配置、构建 CLI 并运行 `doctor` 成功。
- 验证后恢复整合前的桌面 debug 构建配置，CLI 默认关闭；新命令行二进制保留在 `build/macosx/arm64/release/crossdesk-cli`。

## 优化回归（2026-10-30）

- Release 构建、实际二进制 bridge/PTY 回归通过。ASan/UBSan 检查覆盖原有会话和渲染行为，以及无效帧保留、NV12 缓冲复用、预览参数去重、ACK 状态、重复断开和重连网络统计复位。
- 生产 bridge 配合工程外假传输验证静止画面重绘、细节模式、质量切换、窗口变化、无效配置原子性和帧 ACK 背压，通过。
- ANSI 渲染前后对比覆盖 8/24/256 色、UTF-8/ASCII、half/quadrant、全景/细节模式，输出校验值一致。在本机 240×100 网格连续执行 200 次差分生成，静止网格约 447.4 ms → 3.6 ms，单行变化约 422.8 ms → 9.5 ms。该结果只衡量差分文本生成，不包含视频解码、图像缩放和终端显示时间。
- npm 真实打包、离线安装及源码包重建通过；工程外故障注入确认已有产物保留、第二个包生成失败时无半套输出、缺失脱敏补丁时拒绝打包。

新增回归代码、基准、假传输和验证产物均位于工程外临时目录，未向工程或子模块添加测试。此次完整构建与运行验证覆盖 macOS arm64，未重新进行 Linux/Windows 实机验证。

## 独立开发阶段的验证范围

以下为迁入前的验证记录摘要，不表示本次重新连接了远程设备：

- macOS arm64 的 debug/release 构建、CLI bridge 的 NDJSON、POSIX PTY 隐藏密码与终端恢复均已验证。
- 工程外的 ASan/UBSan 检查覆盖会话状态、只读限制、输入释放、帧内存所有权、渲染、缩放和平移、输出背压和帧 ACK。
- Pi 1.4.0 SDK 类型检查与模拟组件生命周期已验证；真实 Pi TUI 视觉效果未验收。
- 早期身份协议版本曾连接真实设备进行只读 ANSI 观看和 Kitty 协议数据验证；Kitty 数据解压检查不等于 Kitty 应用或 SSH 实机显示验收。
- 改为 `cli-` 临时身份后，正式 CLI 与本地 WSS 服务端验证了临时注册、Join、身份不可复用及退出清理；线上服务尚未在该轮部署，早期画面验证不能代替新身份协议的线上联调。
- npm 的 macOS arm64 本地生成及安装已验证；Linux / Windows 当时仅完成 CLI 主程序和平台 I/O 的交叉编译检查，没有完整 MiniRTC 链接和实机连接结果。

## 实测边界

本地检查不涵盖 Linux / Windows 完整构建、跨平台远程控制、Kitty/SSH 实际终端显示、真实 Pi TUI 或长时间资源占用。六个平台 CI 配置与 npm 包名均不代表已发布或已验收。

CLI 需要支持 `cli` 临时注册的服务端。`doctor --json` 仅报告本机环境，不会联网登录或证明远端可连接。
