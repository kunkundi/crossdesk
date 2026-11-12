# CrossDesk MCP

让 Codex 或 DeepSeek Harness 在你的授权下查看、操作远程电脑，并在右侧显示远端画面。你可以用自然语言安排任务，随时暂停操作、接管鼠标键盘或断开连接。

[准备工作](#install) · [下载安装包](#install-package) · [DSH 安装](#install-dsh) · [Codex 安装](#install-codex) · [连接设备](#用户连接流程) · [日常使用](#使用示例) · [常见问题](#install-troubleshooting)

<a id="install"></a>

## 准备工作

需要准备两台电脑：

- **本机**：运行 Codex 桌面版或 DeepSeek Harness（DSH）桌面版，安装 CrossDesk MCP。
- **远端电脑**：安装并运行 [CrossDesk](https://www.crossdesk.cn/)，保持在线，准备好客户端显示的 9 位设备 ID 和访问密码。按系统提示授予屏幕录制、辅助功能等权限，详见 [客户端使用指南](../../docs/USER_GUIDE.md)。

| 你使用的应用 | 本机需要准备什么 | 画面在哪里 |
| --- | --- | --- |
| DeepSeek Harness 桌面版 | 一个 CrossDesk MCP 平台插件包 | 当前对话的右侧浏览器 |
| Codex 桌面版 | Node.js 22 或以上，以及 CrossDesk MCP 平台安装包 | 右侧内置浏览器 |
| Codex CLI | Node.js 22 或以上，以及 CrossDesk MCP 平台安装包 | 系统浏览器或独立终端 |

安装时需要联网下载依赖。MCP 和用于显示画面的应用应在同一台本机上运行。远端电脑使用自建服务器时，在连接面板的“高级设置”中填写相同的服务器和端口。

目前侧栏已在 macOS 的 Codex 和 DSH 0.2.2-rc.1 验证；Harness 网页版不支持此侧栏。其他平台的验证范围见 [开发参考](DEVELOPMENT.md)。

<a id="install-package"></a>

## 下载安装包

DSH 和 Codex 都使用下表中的**平台安装包**，其中已包含所需的原生程序。

1. 登录 GitHub，打开 [CrossDesk 构建页面](https://github.com/kunkundi/crossdesk/actions/workflows/build-cli.yml)。
2. 选择一次成功的运行，在页面的 **Artifacts** 区域下载与你的本机对应的文件。
3. 解压下载的 `.zip`，取出里面的 `.tgz` 安装包，例如 `chunkdash-crossdesk-mcp-0.1.0.tgz`。保留 `.tgz`，安装时直接使用它。

GitHub 要求登录后才能下载构建产物，操作说明见 [下载构建产物](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts)。

| 运行 Codex / DSH 的电脑 | 下载的产物名称 |
| --- | --- |
| Apple Silicon Mac（M 系列芯片） | `crossdesk-mcp-darwin-arm64` |
| Intel Mac | `crossdesk-mcp-darwin-x64` |
| Linux x64（glibc） | `crossdesk-mcp-linux-x64` |
| Linux arm64（glibc） | `crossdesk-mcp-linux-arm64` |
| Windows x64 | `crossdesk-mcp-win32-x64` |

Mac 可在“关于本机”中查看芯片；Windows 可在“设置 → 系统 → 系统信息”中查看系统类型；Linux 可运行 `uname -m` 查看架构，`x86_64` 对应 x64，`aarch64` 对应 arm64。

不同平台解压后的 `.tgz` 文件名相同，请以下载时的产物名称区分。如果某次运行没有表中对应的产物，选择另一次提供该产物的运行；也可以按 [源码构建说明](DEVELOPMENT.md#从源码运行) 自行构建。

下文以 `0.1.0` 为例。**所有 `/absolute/path/...` 都需要换成自己电脑上的完整路径**，版本号也应与实际下载的文件一致。路径中有空格时保留引号。

<a id="install-dsh"></a>

## 在 DeepSeek Harness 中安装

### 1. 安装插件

在 Harness 的插件页面选择本地 `.tgz` 安装包，或在终端执行：

```sh
dsh plugin --profile desktop add "/absolute/path/chunkdash-crossdesk-mcp-0.1.0.tgz"
```

这个插件包会自动提供 MCP 工具和侧栏画面，无需另外安装 Node.js、原生 CLI 或填写 MCP 启动配置。

如果 macOS 终端提示找不到 `dsh`，可使用应用自带的命令：

```sh
"/Applications/DeepSeek Harness.app/Contents/Resources/runtime/cli/bin/dsh" plugin --profile desktop add "/absolute/path/chunkdash-crossdesk-mcp-0.1.0.tgz"
```

上述命令对应默认安装位置和 `desktop` 配置。使用自定义安装位置或 profile 时替换为自己的值。

### 2. 打开 CrossDesk

重启 Harness，在插件页面确认 CrossDesk（`@chunkdash/crossdesk-mcp`）已启用。然后在对话中发送：

> 打开 CrossDesk，让我选择远程电脑。

右侧出现设备连接面板，就可以继续 [连接设备](#用户连接流程)。安装插件本身不会连接任何远端电脑。

<a id="install-codex"></a>

## 在 Codex 中安装

### 1. 安装到固定目录

先安装 Node.js 22 或以上。打开终端，按自己的系统执行以下命令，并替换安装包路径。

macOS / Linux：

```sh
mkdir -p "$HOME/crossdesk-mcp"
cd "$HOME/crossdesk-mcp"
npm init -y
npm install "/absolute/path/chunkdash-crossdesk-mcp-0.1.0.tgz"
```

Windows PowerShell：

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE/crossdesk-mcp"
Set-Location "$env:USERPROFILE/crossdesk-mcp"
npm init -y
npm install "C:/Downloads/chunkdash-crossdesk-mcp-0.1.0.tgz"
```

安装完成后，在同一目录运行：

```sh
node node_modules/@chunkdash/crossdesk-mcp/dist/main.js --help
```

出现命令帮助说明 Node 入口可以运行。请保留安装目录，后续 Codex 会从这里启动 CrossDesk。

### 2. 添加 MCP 配置

打开用户目录中的 `.codex/config.toml`（macOS / Linux 为 `~/.codex/config.toml`），加入以下配置。已有 `[mcp_servers.crossdesk]` 时更新该段，保留其他服务配置。

```toml
[mcp_servers.crossdesk]
command = "/absolute/path/node"
args = [
  "/absolute/path/crossdesk-mcp/node_modules/@chunkdash/crossdesk-mcp/dist/main.js",
  "serve", "--stdio",
  "--panel=browser", "--browser-launch=host"
]
enabled = true
startup_timeout_sec = 30
tool_timeout_sec = 90
```

需要替换两处路径：

- `command`：Node 可执行文件的完整路径。macOS / Linux 用 `command -v node` 查询；Windows PowerShell 用 `(Get-Command node).Source` 查询。
- `args` 第一项：上一步安装目录下的 `node_modules/@chunkdash/crossdesk-mcp/dist/main.js` 完整路径。

配置路径不要使用 `~`。Windows 可使用正斜杠，例如 `C:/Program Files/nodejs/node.exe`。保留 `--panel=browser --browser-launch=host`，让 Codex 桌面版打开右侧浏览器。配置格式可参考 [Codex 官方文档](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)。

### 3. 打开 CrossDesk

重启 Codex，在对话中发送：

> 打开 CrossDesk，让我选择远程电脑。

右侧出现设备连接面板后，按下方步骤授权。首次安装没有已保存设备是正常的。

**使用 Codex CLI 时**，将配置中的 `--browser-launch=host` 改为 `--browser-launch=system`，画面会在本机系统浏览器显示。无图形环境的终端用法见 [显示方式配置](DEVELOPMENT.md#面板策略)。

## 用户连接流程

1. 在右侧面板选择已保存设备，或选择“连接新设备”。
2. 输入远端电脑的 **设备 ID** 和 **访问密码**。密码只在面板中填写，不发送到对话。
3. 选择授权范围：**仅查看画面**适合观察和检查；需要打开网页、输入文字、操作文件时，选择 **允许 Agent 操作**。
4. 点击 **授权并连接**。连接成功后，面板会显示远端桌面、分辨率和帧率。
5. 回到对话下达任务。如果连接前已经安排了任务，Agent 会在连接就绪后继续执行。

### 下次如何直接连接

展开连接面板中的 **保存与授权**，按需设置：

| 选项 | 作用 |
| --- | --- |
| 保存设备 | 记住设备信息，可填写“工作电脑”等备注 |
| 记住密码 | 将访问密码保存在本机系统凭据库 |
| 允许下次自动连接 | 允许 Agent 在以后的任务中沿用本次授权范围连接 |

后两项需要先勾选“保存设备”。希望以后通过设备备注直接连接时，同时保存密码并允许下次自动连接；仅保存设备不会自动授权控制。

例如：

> 使用 CrossDesk 连接“工作电脑”，帮我打开浏览器搜索天气。

如果本次只授权查看，之后需要操作时，请断开后重新连接并选择“允许 Agent 操作”。

## 使用示例

连接后直接向 Agent 描述要做的事情：

| 任务 | 可以这样说 |
| --- | --- |
| 查看屏幕 | “看一下远程电脑现在显示什么。” |
| 操作浏览器 | “在远程浏览器搜索一头牛，打开相关视频。” |
| 查找文件 | “帮我看看远端下载文件夹里有哪些 PDF。” |
| 下载文件 | “把远端桌面的报告下载到本机 Downloads 文件夹。” |
| 切换显示器 | “切换到远程电脑的第二块显示器。” |
| 调整画面 | “把远端预览分辨率降低一些，让画面更流畅。” |

具体操作取决于设备授权、远端系统权限和应用支持。实时画面用于监看；Agent 的操作速度还取决于它观察和决策所需的时间。

### 暂停、接管和结束任务

| 你想做什么 | 面板中的操作 |
| --- | --- |
| 暂停 Agent 操作 | 点击 **暂停 Agent**；需要继续时由你点击 **恢复 Agent** |
| 自己操作远端 | 打开 **会话选项 → 人工接管**，结束后点击 **交还 Agent** |
| 听远端声音 | 打开 **会话选项 → 播放声音**，按需调整音量 |
| 放大画面 | 打开 **会话选项 → 放大画面** |
| 结束远程会话 | 打开 **会话选项 → 断开连接**，或让 Agent 断开 |

人工接管需要本次连接已授权操作。**关闭侧栏不会断开远端连接**；结束使用时请选择“断开连接”。

## 更新与卸载

先结束正在进行的任务，并断开远端连接，再更新或卸载。

**DSH：** 在插件页面安装新版平台包，或重新执行 `dsh plugin ... add` 安装命令，然后重启 Harness。卸载可在插件页面操作，也可运行：

```sh
dsh plugin --profile desktop remove @chunkdash/crossdesk-mcp
```

**Codex：** 在原来的 `crossdesk-mcp` 安装目录执行 `npm install "/absolute/path/新版安装包.tgz"`，然后重启 Codex。停用时将配置中的 `enabled` 改为 `false`；卸载时移除对应配置段，并在安装目录执行 `npm uninstall @chunkdash/crossdesk-mcp`。

卸载不会清除保存在本机的设备配置和系统凭据。

<a id="install-troubleshooting"></a>

## 常见问题

| 遇到的问题 | 如何处理 |
| --- | --- |
| 找不到下载文件或下载按钮 | 登录 GitHub，确认所选运行的 Artifacts 中包含对应平台的 `crossdesk-mcp-...` 文件；产物失效时选择其他可用运行 |
| DSH 提示找不到 `dsh` | 使用插件页面安装；macOS 也可使用上文的应用内完整命令 |
| DSH 安装后没有 CrossDesk 工具 | 确认插件已启用并重启 Harness；在插件页面查看错误。若报告原生程序缺失，重新安装对应平台的安装包 |
| Codex 没有 CrossDesk 工具 | 检查 Node 版本和配置中的两处完整路径，先运行 `--help`，再重启 Codex。安装了 Codex CLI 时也可用 `codex mcp list` 查看配置 |
| 工具可用，但没有右侧画面 | 确认使用桌面版；Codex 保留上文的两项显示参数。让 Agent“重新打开当前 CrossDesk 连接的画面” |
| 页面打开后无法连接设备 | 确认远端在线、设备 ID 和访问密码正确；使用自建服务器时核对面板的“高级设置” |
| 已连接，但黑屏或无法操作 | 检查远端 CrossDesk 的屏幕录制、辅助功能等系统权限；只有查看权限时，重新连接并授权操作 |
| 能连接，但某项功能提示不支持 | 检查远端客户端版本，按提示更新兼容版本；窗口和应用操作还取决于远端系统及应用支持 |
| 无法保存密码 | 检查本机系统凭据库是否可用；也可以取消“记住密码”，每次在面板手动输入后连接 |
| 画面卡顿 | 让 Agent 降低预览分辨率或画质，并检查两端网络。画面帧率与 Agent 的决策速度是不同的指标 |
| 没有声音 | 在面板点击“播放声音”，并确认远端允许录制桌面声音 |
| 远端锁屏了 | 按面板的解锁提示输入系统锁屏密码；它与 CrossDesk 访问密码分别填写 |
| 提示连接或页面入口过期 | 让 Agent 重新打开当前连接的画面，无需重新建立远端连接 |
| 提示 `DEVICE_BUSY` | 先断开其他 Agent 或 MCP 进程对同一设备的连接 |
| 提示 `serverName ... already in use` | 检查是否重复安装或配置了 CrossDesk 服务，只启用一个实例 |
| 提示 `NATIVE_PACKAGE_MISSING` 或 `NATIVE_PROTOCOL_MISMATCH` | 重新安装表中对应平台的完整包；源码运行请检查 [原生程序配置](DEVELOPMENT.md#从源码运行) |
| 为什么没有自动重连 | 在 **会话详情** 中按需启用“断线自动恢复”；此功能需要已保存的自动连接授权和可用凭据，主动断开不会触发恢复 |

如果仍有问题，可 [提交反馈](https://github.com/kunkundi/crossdesk/issues)，说明本机和远端系统、Codex / DSH 版本、安装包来源、报错内容及复现步骤。不要附上设备密码或带配对凭据的页面地址。

<a id="从源码运行"></a>

## 更多文档

- [源码构建与开发配置](DEVELOPMENT.md#从源码运行)
- [工具接口、显示机制与验证范围](DEVELOPMENT.md)
- [DSH 安装简明指南](integrations/deepseek-harness/README.md)
