# 组件版本管理

在仓库根目录使用 `python3 scripts/version.py` 查看、修改和检查版本，需要 Python 3.9 或更新版本，无需安装第三方依赖。版本分为三组：桌面、Android、iOS 和 HarmonyOS 共用客户端版本；CLI、MCP 和 DSH 插件共用工具版本；Pi 扩展独立编号。脚本只修改源码配置，不会创建 Git 标签、提交代码、构建或发布安装包。

## 查看和检查

```sh
python3 scripts/version.py show
python3 scripts/version.py show client
python3 scripts/version.py show mcp
python3 scripts/version.py show --json
python3 scripts/version.py show desktop --field version
python3 scripts/version.py check
```

`check` 校验版本格式、移动端构建号、重复字段和分组版本一致性。发现不一致时返回非零退出码并指出组件；用 `set client <版本>` 统一客户端，用 `set cli <版本>` 或 `set mcp <版本>` 统一 CLI、MCP、DSH 及原生依赖。单独检查任一客户端都会检查全部客户端；`check cli` 和 `check mcp` 也都会检查整个工具分组。

`Check component versions` GitHub Action 在 push、PR 和手动运行时执行全部检查。各构建及 npm 发布工作流也会检查对应分组，不一致时停止。版本脚本不查询 npm 或应用商店；正式发布仍使用对应工作流的发布前检查。

## 可管理的组件

| 组件参数 | 版本来源及同步范围 |
| --- | --- |
| `client` | 统一管理所有客户端版本，分别保留 Android、iOS、HarmonyOS 的构建号；`mobile` 是同一分组的别名，也会同步桌面版本 |
| `desktop` | 修改版本时同步全部客户端；版本写入 `apps/desktop/xmake/options.lua` 的 `CROSSDESK_VERSION` 默认值，供 Windows、macOS、Linux 共用 |
| `android` | 修改版本时同步全部客户端；`--build` 和 `bump android build` 只指定 Gradle 的 `crossdeskVersionCode` |
| `ios` | 修改版本时同步全部客户端；`--build` 和 `bump ios build` 只指定 `CURRENT_PROJECT_VERSION`，同步 Debug 和 Release |
| `harmony` | 修改版本时同步全部客户端；`--build` 和 `bump harmony build` 只指定 `AppScope/app.json5` 的 `versionCode` |
| `cli` | 同步 CLI、MCP、DSH 和 MCP 的 CLI 依赖；CLI 写入 `apps/cli/package.json` 与 `apps/cli/xmake.lua`，五个平台原生 npm 包及入口包在打包时继承此版本 |
| `mcp` | 与 `cli` 修改范围相同；MCP 写入 `apps/mcp/package.json`、锁文件中的根版本与根包版本、DSH 集成目录的包版本，以及 `src/native.ts` 的 `NATIVE_VERSION` |
| `pi` | `apps/cli/plugins/pi/package.json` 的扩展版本 |

npm 组件若有 `package-lock.json`，脚本会同步其中的根版本和 `packages[""].version`，不会改动依赖包版本。DSH 与 MCP 一起分发，不需要独立设置 DSH 发布版本。HarmonyOS 的模块描述、SDK `modelVersion` 和本地类型库版本不属于应用发布版本，保持各自的用途。

## 设置版本

以下均为命令示例，替换为准备发布的版本：

```sh
python3 scripts/version.py set client 1.7.7 --android-build 5 --ios-build 7 --harmony-build 10002
python3 scripts/version.py set cli 0.3.0
python3 scripts/version.py set mcp 0.3.0
python3 scripts/version.py set pi 0.1.1
```

所有修改命令都支持 `--dry-run`，先显示将要修改的文件差异：

```sh
python3 scripts/version.py set mcp 0.3.0 --dry-run
```

版本参数填写 `X.Y.Z`，不包含标签前缀 `v`、日期或构建元数据。CLI、MCP、Pi 另支持 `X.Y.Z-rc.N` 等预发布版本。客户端共用版本各段限制在 `0–65535`，以适配 Windows 版本资源。

`set client` 可分别传入 `--android-build`、`--ios-build`、`--harmony-build`，不接受含义不明确的 `--build`。`set android 1.7.7 --build 5` 同样会将全部客户端发布版本设为 `1.7.7`，但 `--build 5` 只指定 Android 构建号，其余移动端按各自的构建号递增。`set ios`、`set harmony` 对称处理；`set desktop` 同样同步全部客户端，但不接受 `--build`。

各端发布版本变化而未显式指定构建号时，该端构建号自动加一；相同版本且未指定构建号时不改动文件。例如从所有客户端 `1.7.6`、Android / iOS / HarmonyOS 构建号分别为 `4` / `6` / `10001` 执行 `set client 1.7.7`，结果为所有客户端 `1.7.7`、构建号 `5` / `7` / `10002`。修复版本差异时，仅版本实际变化的端自动增加构建号。显式构建号不得降低，修改应用版本时构建号必须增加。脚本统一使用正整数构建号：Android 最大 `2100000000`，iOS 最大 `9999`，HarmonyOS 最大 `2147483647`。

修改前会先验证全部目标字段，验证失败不写文件；写入保留文件格式和权限，并使用逐文件原子替换。发生写入错误时会尝试还原已经写入的文件，修改后仍应检查 `git diff`。

## 递增版本

```sh
python3 scripts/version.py bump cli patch
python3 scripts/version.py bump mcp minor
python3 scripts/version.py bump client patch
python3 scripts/version.py bump client major --dry-run
python3 scripts/version.py bump android build
```

`major`、`minor`、`patch` 递增对应数字，并清零较低位；例如 `1.2.3` 递增 `minor` 得到 `1.3.0`。`bump client patch` 和对任一客户端执行 `bump ... patch` 都会同步提升全部客户端发布版本，并分别增加移动端构建号。`bump cli` 和 `bump mcp` 都会同步 CLI、MCP、DSH 和 MCP 的原生依赖。分组内原本不一致时，先使用 `set` 明确统一到哪个版本。

`bump android build`、`bump ios build`、`bump harmony build` 只增加指定端的构建号，其余端保持不变；不提供 `bump client build` 或 `bump mobile build`。当前是预发布版本时使用 `set` 明确指定下一个版本，例如 `set mcp 0.2.0-rc.2` 或 `set mcp 0.2.0`。

## CLI、MCP 与 DSH 的共同版本

CLI、MCP、DSH 集成元数据和 MCP 的原生依赖版本必须完全相同，当前均为 `0.2.0`。执行 `set cli 0.3.0` 或 `set mcp 0.3.0` 会将它们一起设为 `0.3.0`，只需执行其中一条命令。预发布后缀也必须一致。不提供单独设置原生依赖版本的参数。

MCP 通用 npm 包从 `NATIVE_VERSION` 生成五个平台包的精确依赖；MCP 的 `--version` 和协议握手读取包版本。DSH 随 MCP 同包分发，不另发插件版本。

先通过 CLI 发布工作流发布同版本的五个平台原生包和 CLI 入口，再发布 MCP。即使只修改 MCP，也要为两者准备同一个新版本并按此顺序发布。CLI 与 MCP 仍使用各自的发布工作流和标签前缀；例如同一次版本更新使用 `cli-v0.3.0` 与 `mcp-v0.3.0`，建议指向同一提交。

CLI 构建工作流为每个平台附带生成 `crossdesk-mcp-<平台>` 安装包。源码版本不一致时，构建、MCP 打包和发布检查会报错；平台安装包还要求传入的原生包版本一致。工具版本不影响客户端版本和 Pi 扩展版本。

## 与构建和发布配合

客户端版本同时控制本地 Xmake 默认值，以及 GitHub Actions 分支构建未填写 `desktop_version` 时的默认版本。已有 Xmake 配置可能保留缓存，应重新配置；显式 `--CROSSDESK_VERSION`、Actions 的 `desktop_version` 和发布标签继续按原流程覆盖默认值。正式发布时应使标签的基础版本与客户端源码版本一致。日期和热修订后缀仍由发布流程处理，本脚本不修改既有标签格式。各端可分开构建和发布，但共用同一个源码发布版本。

修改并检查后，审阅、提交和推送源码，再按实际版本创建对应标签：CLI 使用 `cli-v<版本>`，MCP 使用 `mcp-v<版本>`。移动端沿用 `android-v<版本>-YYYYMMDD`、`ios-v<版本>-YYYYMMDD`。已发布版本不能用不同内容覆盖；版本修改脚本不会替你判断某个版本是否已在注册表中使用。

相关说明：[桌面构建](BUILD.md) · [CLI 发布](../apps/cli/docs/npm-distribution.md#ci-与发布) · [MCP 发布](../apps/mcp/DEVELOPMENT.md#github-actions-发布到-npm)。
