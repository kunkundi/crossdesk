# CrossDesk 文档导航

[项目首页](../README.md) · [English user guide](USER_GUIDE_EN.md)

## 第一次使用

1. [下载并选择客户端](../README.md#download)。
2. [安装与授予系统权限](USER_GUIDE.md#installation)。
3. [通过设备 ID 连接](USER_GUIDE.md#quick-start)，或在局域网中 [IP 直连](USER_GUIDE.md#lan-direct)。
4. 连接不成功时查看 [常见问题](FAQ.md)。

## 按使用场景查找

| 场景 | 文档 |
| --- | --- |
| 找到桌面上的操作入口 | [桌面使用指南](USER_GUIDE.md) |
| 调整画面、声音，收发文件 | [会话操作](USER_GUIDE.md#session) |
| 集中管理多台电脑 | [桌面墙](deskhub.md) |
| 限制哪些设备可以连接 | [可信设备与配对](CONNECTIONS.md#trusted-devices) |
| 重装或迁移本机身份 | [备份与迁移](CONNECTIONS.md#backup) |
| 更新客户端 | [更新与故障处理](UPDATER.md) |
| 用手机或平板控制电脑 | [iPhone / iPad](../apps/ios/README.md) · [Android](../apps/android/README.md) · [鸿蒙](../apps/harmony/MOBILE_PARITY.md) |
| 用终端连接 | [CLI 使用指南](../apps/cli/README.md) |
| 让 Agent 操作电脑并监看 | [MCP 安装与使用](../apps/mcp/README.md) |
| Windows 登录前访问 | [无人值守](USER_GUIDE.md#windows-service) |
| Linux 无显示器运行 | [无头运行](HEADLESS.md) |
| 使用自己的服务器 | [自托管](SELF_HOSTING.md) |
| 自己编译客户端 | [构建指南](BUILD.md) |
| 切换界面语言 | [语言设置](../localization/README.md) |

## 构建与集成

| 主题 | 文档 |
| --- | --- |
| 桌面构建、打包与签名 | [中文](BUILD.md) · [English](BUILD_EN.md) · [Linux 构建镜像](../docker/linux-build/README.md) |
| GUI 分层、状态与代码归属 | [GUI 架构](gui-architecture.md) |
| ID / IP 寻址、认证与会话生命周期 | [连接架构](connection-architecture.md) |
| 桌面墙订阅与带宽接口 | [预览和传输协议](deskhub.md#传输实现与接口) |
| CLI 渲染、输入与终端适配 | [终端客户端](../apps/cli/docs/terminal-client.md) · [验证记录](../apps/cli/docs/validation.md#验证记录) |
| 终端和自动化接入 | [Bridge](../apps/cli/docs/bridge-protocol.md) · [原生自动化接口](../apps/cli/docs/automation-protocol.md) |
| MCP 工具与宿主集成 | [构建与集成](../apps/mcp/DEVELOPMENT.md) |
| 移动端构建与平台行为 | [Android](../apps/android/README.md#构建) · [iOS](../apps/ios/README.md#build) · [HarmonyOS](../apps/harmony/README.md#构建) |
| 鸿蒙桌面适配与权限 | [PC 平台说明](../apps/harmony/desktop-port/README.md) |
| 组件版本与 npm 发布 | [版本管理](VERSIONING.md) · [CLI 分发](../apps/cli/docs/npm-distribution.md#生成包) · [MCP 发布](../apps/mcp/DEVELOPMENT.md#github-actions-发布到-npm) |
| 更新签名、清单与升级流程 | [升级器构建与发布](UPDATER.md#升级器构建与发布) |
| 翻译资源与截图 | [本地化维护](../localization/README.md#维护资源) · [截图生成](images/REGENERATE.md) |

开发版功能可能尚未包含在已发布安装包中，请以所用版本的说明与界面为准。反馈问题时提供两端系统、版本、连接方式和复现步骤，桌面端可通过 **主菜单 → 反馈与建议** 导出诊断日志。
