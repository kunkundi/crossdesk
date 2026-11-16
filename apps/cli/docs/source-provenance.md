# CLI 源码与许可信息

[返回 CLI 指南](../README.md)

CrossDesk CLI 的源码位于 [CrossDesk 仓库](https://github.com/kunkundi/crossdesk)。下载某个版本的安装包后，请使用包内记录的源码版本和构建信息定位对应源码，不要把当前分支当作该安装包的精确来源。

CLI 使用 [MiniRTC](https://github.com/kunkundi/minirtc)；具体版本由主仓库的 `deps/submodules/minirtc` 子模块提交记录。克隆时使用 `--recurse-submodules`，或在已有仓库执行 `git submodule update --init --recursive`。更新依赖后需重新构建 CLI。

npm 原生包附带 `source/` 源码快照，保留 CLI、共享库、构建配置和 MiniRTC 的目录结构。需要自行编译时，在源码目录按 [构建步骤](terminal-client.md#构建与安装) 配置 `--CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y`。

CrossDesk 使用 [GPL-3.0-only](../../../LICENSE)，MiniRTC 保留其 LGPL-3.0 及第三方许可声明。其他依赖分别适用各自许可证；具体材料以所用版本附带的许可证和源码说明为准。

## 整合来源与依赖变更

CLI 从独立开发目录整合到 CrossDesk，入口、打包脚本、npm 启动器和 Pi 扩展位于 `apps/cli`，会话和终端渲染实现分别位于 `libs/controller`、`libs/terminal`。

- CrossDesk 仓库：`https://github.com/kunkundi/crossdesk.git`。独立 CLI 的协议代码原取自 `becd1c0437f6c4fad757052c70db1eba71cf312f`；整合时目标仓库基于 `2f57d0ab`，两边 wire 源文件一致，直接复用现有 `libs/wire`。
- MiniRTC：`https://github.com/kunkundi/minirtc.git`，版本由主仓库记录的 `deps/submodules/minirtc` 子模块提交固定。整合时采用的 `b9ee1fa7fe038fdf771f5dec1b1d561d78273f15` 基于整合前的 `cdcd0d8fb0e9b688f3bf6416fe52b634cf20d9ca`，仅增加下述四处凭据日志脱敏修改。
- `ControllerSession` 的流名称、HostInformation 和输入参考 Android `rtc_bridge.cpp`；身份使用单 Peer 的 `cli` 临时注册协议；终端适配不依赖 JNI、Surface 或 GuiRuntime。
- 帧缓存遵守 `MiniRtcVideoFrame` 回调借用契约，直接复制到有界 CPU 存储，不跨回调保留 native 指针。
- PreviewSubscription 复用 wire 实现与对端 capability 字段，不新增被控端协议。
- MCP 实时面板新增 MiniRTC 的 `on_receive_encoded_video` 回调，转发解码前的完整 H.264 Annex B 帧；回调及关键帧请求由当前子模块源码提供。`MiniRtcParams` 新增字段改变 ABI 大小，CLI 与 MiniRTC 必须一起重建；发布时同步记录包含该改动的子模块提交。
- 自动化图像编码复用 MiniRTC 已有的 libyuv 2025.8.14，并通过 Xmake 固定静态 libpng v1.6.59；libpng 的压缩依赖沿用平台 zlib。新增依赖的许可证和对应源码也应纳入正式发行的依赖材料。

MiniRTC 已在提交 `b9ee1fa7fe038fdf771f5dec1b1d561d78273f15` 中合入凭据日志脱敏：Join、排队 Join、取消 Join、Leave 四个日志点只记录 `@` 前的 ID，不记录密码。`apps/cli/scripts/prepare-deps.sh` 仅按主仓库记录初始化子模块，不再应用独立补丁；更新 MiniRTC 时只需更新子模块提交。共享子模块中的脱敏同样适用于桌面程序；该修改没有改变鉴权、TLS、TURN 或媒体协议。

npm 原生包携带 CLI、共享库、构建配置和 MiniRTC 的源码快照，不复制桌面、移动端、测试或本机构建缓存。源码包保留仓库目录结构，在 `source/` 中配置 `--CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y` 后构建。

LICENSE 沿用 CrossDesk GPL-3.0；MiniRTC 子模块保留其 LGPL-3.0 和第三方声明。外部 Xmake 依赖的许可证、对应源码及重链接材料仍需随正式版本提供。
