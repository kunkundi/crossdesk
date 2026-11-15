# CrossDesk MCP 进阶使用与配置

[返回安装与使用指南](README.md)

已使用平台安装包连接成功的用户通常无需修改这里的配置。本页用于自行构建、选择画面显示方式，或在没有图形界面的本机上配置连接。

## 安装方式与版本

优先使用 [平台安装包](README.md#install-package)，它包含所需的原生程序。源码方式需要单独构建 CLI 和 MCP，并指定原生程序的完整路径。

CLI、MCP 和 DSH 插件需使用匹配版本。遇到 `NATIVE_PACKAGE_MISSING` 或 `NATIVE_PROTOCOL_MISMATCH` 时，重新安装同一版本的完整平台包，或重新构建同一份源码中的 CLI 和 MCP。全局安装 CLI 不会使 MCP 自动找到它。

## 从源码运行

先下载源码；已有仓库可跳过克隆：

```sh
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
```

在仓库根目录构建原生程序（已有工作区可保留原有桌面构建选项）：

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

## DSH 源码安装配置

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
| `nativeBinary` | 包内原生程序，或固定版本的平台依赖 | 源码安装或显式使用兼容 CLI，必须为绝对路径 |
| `configDir` | `~/.config/crossdesk/mcp/` | 指定设备配置目录，必须为绝对路径 |
| `serverName` | `crossdesk` | MCP 工具命名空间；侧栏会同步使用该名称 |

用户配置覆盖整个 `config` 时需保留仍然需要的字段。Windows 路径可用 `C:/.../crossdesk-cli.exe`。集成机制参考 [Harness bundle](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/boot/app-boot#profiles) 与 [MCP Client](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/mcp/mcp-client)。

## 面板策略

在 MCP 启动参数中设置 `--panel`。命令行选项优先于保存的设备配置。

| 选项 | 适用场景 |
| --- | --- |
| `--panel=auto` | 默认。Codex 和 DSH 桌面版使用侧栏浏览器；其他环境按可用条件选择浏览器、终端或无面板 |
| `--panel=browser` | 在本机浏览器页面选择设备、授权及监看 |
| `--panel=terminal` | 在另一个交互终端选择设备和观看画面 |
| `--panel=off` | 无实时面板，提前通过本机交互式 `setup` 配置授权与凭据 |
| `--panel=apps` | 使用支持 MCP Apps 的宿主面板；不支持时返回 `PANEL_UNSUPPORTED` |

### Codex 默认侧栏浏览器

Codex 桌面版保留 `--panel=browser --browser-launch=host`。MCP 与浏览器需运行在同一台电脑，Agent 会打开右侧内置浏览器；无需另外编写个人工作约定。

没有内置浏览器的 Codex CLI 可改为 `--panel=browser --browser-launch=system`，使用本机系统浏览器。若无法自动打开，按工具返回的本地配对命令在自己的终端执行。

画面入口过期时，让 Agent 重新打开当前连接的画面即可，无需重新连接远端。配对入口包含临时凭据，不要把地址或启动页内容作为反馈附件。

### DeepSeek Harness 桌面侧栏

安装 [DSH 平台插件包](README.md#install-dsh) 后，插件会自动打开当前对话的右侧浏览器。Harness 网页版不提供相同侧栏路径。

如果工具正常但没有画面，先确认使用桌面版、插件已启用，以及 MCP 与浏览器在同一台电脑。重装后仍无效时提供 DSH 版本和错误内容。

### 终端面板

启动参数选择 `--panel=terminal` 后，让 Agent 打开 CrossDesk。它会提供本次视图的命令，在同一台电脑的另一个交互终端执行，例如：

```sh
crossdesk-mcp view --view <view_id> --config-dir <相同的配置目录>
```

将占位符替换为返回的值。源码方式使用 `node /absolute/path/apps/mcp/dist/main.js` 替换 `crossdesk-mcp`。不要在捕获输出的后台命令或 MCP 自身的 stdio 通道中运行交互面板。

该面板复用现有会话，不会另建一次远程连接。无可用交互终端时会返回 `VIEW_REQUIRES_TERMINAL`。

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

## 多设备、恢复连接与授权

最多可以同时连接 8 台设备。设备备注用于区分不同电脑；给 Agent 安排任务时说明目标设备，避免在多个会话之间混淆。

**会话详情 → 断线自动恢复** 可恢复意外断线，需要已保存的自动连接授权和可用密码。重试次数可设为 1–5 次，默认 3 次；主动断开不会触发恢复。电脑密码变化后需要重新输入并保存。

只允许查看时，Agent 不能操作键鼠或文件。需要扩大授权范围时，断开后重新连接并选择允许操作。人工暂停只能由用户恢复；人工接管结束后点击“交还 Agent”。关闭侧栏不会断开连接。

## 远程操作的适用范围

| 需求 | 使用方式与限制 |
| --- | --- |
| 多显示器 | 让 Agent 切换或查看指定显示器；支持情况取决于远端 |
| 调整画面 | 请求降低预览质量、分辨率或帧率；实际流畅度受网络和设备影响 |
| 文件与剪贴板 | 需要控制授权及兼容的被控端；明确源、目标路径及传输方向 |
| 声音 | 默认关闭，从会话选项开启；远端需要允许桌面声音采集 |
| 窗口与应用操作 | 依赖远端系统、权限和应用可访问性支持 |
| 锁屏与解锁 | 按面板提示输入系统密码；它与 CrossDesk 连接密码不同 |

实时画面用于你监看；Agent 的判断使用截图或其他观察结果。画面帧率与 Agent 执行任务的速度不同。

## 排障资料

先查看 [安装与连接常见问题](README.md#install-troubleshooting)。反馈时附上本机与远端系统、MCP 包版本、宿主版本、显示方式及错误。不要附上密码、保存的凭据或一次性配对地址。

需要自行接入脚本时，可参考 [原生自动化接口](../cli/docs/automation-protocol.md)。普通使用直接在对话中描述任务，无需手工调用接口。
