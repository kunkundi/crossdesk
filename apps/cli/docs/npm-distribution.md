# CLI 安装与 npm 分发

[返回 CLI 指南](../README.md) · [终端操作](terminal-client.md) · [打包](#生成包) · [CI 与发布](#ci-与发布)

## 通过 npm 安装

准备 **Node.js 22 或以上**和 npm，然后执行：

```sh
npm install -g @chunkdash/crossdesk-cli@latest
crossdesk-cli doctor --json
crossdesk-cli 123456789
```

将 `123456789` 换成电脑的 9 位设备 ID，按终端提示隐藏输入密码。`doctor` 仅检查本机环境，不会发起远程连接。

npm 会下载匹配的原生程序，无需安装编译工具。请安装统一入口 `@chunkdash/crossdesk-cli`，并保留可选依赖，不要使用 `--omit=optional`。仅安装平台包不会注册 `crossdesk-cli` 命令。

无需全局安装时可以使用：

```sh
npx @chunkdash/crossdesk-cli@latest doctor --json
npx @chunkdash/crossdesk-cli@latest 123456789
```

首次运行会下载对应包。

## 系统与终端要求

| 系统 | 架构 | 运行要求 |
| --- | --- | --- |
| macOS | Apple Silicon / Intel | 最低版本以所下载包的 `build-info.json` 为准 |
| Linux | x64 / arm64 | glibc、libstdc++、zlib；所需版本以包内 `build-info.json` 为准 |
| Windows | x64 | Windows 10+，支持 Console / ConPTY 和 VT 输出的终端 |

Alpine / musl 没有预编译包。平台包根据 Node 运行时的系统和架构选择；遇到架构不符时先核对 Node 与操作系统架构。

普通终端使用 ANSI 字符画面。Kitty / Sixel 像素画面需要终端支持并通过能力检测；遇到不支持提示时改用 `--renderer ansi`。SSH 需要分配 PTY（`ssh -t`），tmux / screen 使用 ANSI。Windows 的 Git Bash 建议运行在 Windows Terminal 中。

交互观看不支持 `TERM=dumb`，也不能将输出重定向到文件。各平台的运行确认范围见 [兼容性说明](validation.md)；安装成功不代表所有终端都能显示像素画面。

## 更新、旧版迁移与卸载

更新时重新执行：

```sh
npm install -g @chunkdash/crossdesk-cli@latest
```

旧的无作用域包 `crossdesk-cli` 不会自动迁移。若曾安装它，先卸载旧入口再安装新版：

```sh
npm uninstall -g crossdesk-cli
npm install -g @chunkdash/crossdesk-cli@latest
```

卸载新版：

```sh
npm uninstall -g @chunkdash/crossdesk-cli
```

## 安装失败或命令不可用

| 现象 | 处理方法 |
| --- | --- |
| `crossdesk-cli` 找不到 | 确认安装的是统一入口；全局安装后检查 npm 全局命令目录是否在 PATH，或用 `npx` 运行 |
| 本地安装后无法直接运行 | 本地命令位于 `node_modules/.bin`，可在安装目录使用 `npx crossdesk-cli` |
| 缺少原生平台包 | 取消省略可选依赖的设置，重新安装；检查下载网络及所选版本是否提供对应平台 |
| 提示 Node 版本不支持 | 更新到 Node.js 22 或以上后重新安装 |
| `GLIBC` / `GLIBCXX` 或系统版本错误 | 核对包内运行要求，使用满足要求的系统，或按 [自行构建](terminal-client.md#构建与安装) 生成适合本机的程序 |
| 画面模式不支持 | 使用 `--renderer ansi`，像素画面需要终端自身支持 Kitty 或 Sixel |
| `doctor` 正常但无法连接 | 继续检查远端在线状态、ID、密码和服务器，见 [连接排障](terminal-client.md#故障排查与边界) |

反馈时附上操作系统、CPU 架构、Node 版本、终端名称和具体错误。不要把连接密码写入命令或反馈内容。

## 生成包


先按 [构建说明](terminal-client.md) 构建 release。macOS 打包前需要签名，CI 使用 ad-hoc 签名；本地可执行以下命令（Intel 将 `arm64` 换成 `x86_64`）：

```sh
codesign --force --sign - build/macosx/arm64/release/crossdesk-cli
```

然后生成 npm 包：

```sh
npm --prefix apps/cli run pack:npm
```

脚本读取 `apps/cli/package.json` 与 `apps/cli/xmake.lua` 的版本，检查二者一致；检查当前平台二进制格式、CPU、动态链接依赖，并运行离线 `doctor --json`。macOS 只接受系统动态库，Linux 只接受列明的基础系统运行库，Windows 拒绝未随产品处理的 DLL 和动态 MSVC 运行库。Windows 在含 `dumpbin.exe` 的 Visual Studio 开发者环境中运行打包。脚本预先检查两个输出文件是否存在，并在临时目录中完成两个包后再写入输出目录；写入失败时清理本次已写入的包，已有同名产物不会被覆盖。

默认从 `build/<platform>/<arch>/release/` 读取原生程序，将两个 `.tgz` 写入 `build/npm/`。可指定路径：

```sh
npm --prefix apps/cli run pack:npm -- --binary /absolute/path/crossdesk-cli --out-dir /absolute/path/output
```

每台构建机生成当前平台的原生包与相同版本的通用启动包。以 macOS arm64、版本 0.1.0 为例，文件名为 `chunkdash-crossdesk-cli-darwin-arm64-0.1.0.tgz` 和 `chunkdash-crossdesk-cli-0.1.0.tgz`；npm 将 `@scope/name` 的 tarball 文件名写作 `scope-name-version.tgz`。五个 CI artifact 各含一份入口包，最终需要五个平台包和一份入口包，共六个 tarball。

`apps/cli/npm/platforms.json` 集中定义五个平台；脚本校验平台包名合法且不重复，启动包通过精确版本的 `optionalDependencies` 让 npm 仅安装匹配系统和 CPU 的原生包。生成的六个包都包含作者、源码仓库、主页、问题反馈地址，以及面向 npm 官方注册表的公开发布配置。原生包包含二进制、运行要求、SHA-256，以及当前控制端和 MiniRTC 源码快照。外部 Xmake 依赖的许可证、对应源码和重链接材料需要随正式版本一并提供，参见 [源码来源](source-provenance.md)。源码快照已含 MiniRTC，解包后无需运行 `prepare-deps.sh`；在 `source/` 目录按上述构建说明显式关闭桌面、开启 CLI，再构建 `crossdesk-cli`。源码包仅包含 CLI 及其依赖，不包含桌面或移动应用。

`apps/cli/package.json` 的 `private: true` 表示源码目录不是发布产物。只发布打包脚本生成的 tarball；生成的包不带 private 标记。

## 本地安装验证

以 macOS arm64 为例，用生成包的绝对路径，在工程外的临时目录执行：

```sh
npx --yes --offline \
  --package /absolute/path/chunkdash-crossdesk-cli-darwin-arm64-0.1.0.tgz \
  --package /absolute/path/chunkdash-crossdesk-cli-0.1.0.tgz \
  -- crossdesk-cli doctor --json
```

也可先 `npm install --ignore-scripts` 安装这两个本地 tarball，再使用安装目录中的 `crossdesk-cli`。验证交互观看需要真实 TTY 与授权的被控端，doctor 不会建立远程连接。

Pi 等宿主应直接启动原生程序以可靠控制进程和专用密码描述符。全局安装后，用 `crossdesk-cli --print-binary-path` 获取路径，再将该路径设为 `CROSSDESK_CLI`；不要把整条 npx 命令放进此变量。

## CI 与发布

`.github/workflows/build-cli.yml` 配置五个平台的原生 release 构建、doctor 和打包，并上传 tarball。普通 push、PR 和手动构建只生成 artifact；`.github/workflows/release-cli.yml` 复用同一构建流程，再汇总并发布六个 npm 包。`cli-v*` 标签只进入 CLI 发布工作流，不重复触发独立 CLI 构建或桌面发布。更新 MiniRTC 引用时，必须先将对应提交推送到子模块远端，再推送 CrossDesk，否则全新检出和 CI 会在拉取子模块时失败。

### GitHub Actions 自动发布

| 操作 | 结果 |
| --- | --- |
| 推送普通提交 / 提交 PR | 构建五个平台、上传 artifact，不发布 npm |
| Actions → Publish CLI to npm → Run workflow，保持 `dry_run` 勾选 | 完整构建并执行六个包的发布预演，不写入 npm |
| 推送 `cli-v0.1.1` | 校验源码版本为 `0.1.1`，构建并发布到 `latest` |
| 推送 `cli-v0.2.0-rc.1` | 校验源码版本为 `0.2.0-rc.1`，构建并发布到 `next` |
| 在对应 `cli-v*` 标签上手动运行并取消 `dry_run` | 执行真实发布；分支上不允许关闭预演 |

准备下一版时，在仓库根目录运行 `python3 scripts/version.py set cli <版本>`，同步 CLI、MCP、DSH 插件及 MCP 的原生依赖版本，再执行 `python3 scripts/version.py check cli`。`set mcp <版本>` 的修改范围相同，详见[组件版本管理](../../../docs/VERSIONING.md)。先发布 CLI 的平台包和入口包，再发布同版本 MCP。提交并推送代码后创建同版本标签。例如源码已更新为 `0.2.1` 时：

```sh
git tag cli-v0.2.1
git push origin cli-v0.2.1
```

已经上传过 `0.1.0` 的包不能被新构建覆盖；下一次原生程序发布应使用新版本。手动预演使用 `npm pack --dry-run` 加上内容校验，不调用 `npm publish` 或交换 OIDC 凭据；可对分支运行，也允许检查已发布的版本，但不验证 npm 发布权限。标签运行始终校验版本一致性。GitHub 的手动运行入口需要该工作流先进入默认分支。

发布 job 只下载当前运行的五个 `npm-*` artifact，并保留独立目录。`release-npm.cjs` 校验包名、版本、平台、入口依赖、原生程序 SHA-256 和 tarball SHA-512；五份入口包的文件内容必须一致（允许 Windows CRLF 差异），实际采用 Linux x64 构建生成的入口，保留 Unix 启动权限。真实上传前先检查所有包的注册表状态，同版本内容不同或目标 dist-tag 已指向更新版本就停止。

上传顺序为五个平台包 → 等待其精确版本、完整性及 dist-tag 可查询 → 入口包 → 最终确认。npm 返回异步处理时，每 15 秒查询一次，每批最多等待 15 分钟；不会因查询不到版本就重复上传。真实发布带 provenance，使用 OIDC 短期凭据，只有发布 job 获得 `id-token: write`，整个流程不需要 `NPM_TOKEN`。发布 job 串行执行，不中断正在上传的版本。

如果发布 job 失败，优先使用 GitHub 的 **Re-run failed jobs**，复用本次已成功构建的 artifact。已存在且完整性、目标 dist-tag 一致的包会跳过；同版本不同内容、仍为 `0.0.0-stage` 的占位包、403 或网络错误都会停止并给出原因。等待 npm 处理完成或修复权限后再重跑，不要直接重跑全部构建；重新编译可能产生不同字节。如果已有版本的 dist-tag 与本次目标不一致，需要先人工检查并调整标签。本工作流不自动修改已有版本的 dist-tag，也不提供 `next` 到 `latest` 的推广操作。

### npm 账号一次性绑定

在下面六个包各自的 npm **Settings → Trusted Publisher** 中选择 **GitHub Actions**，不是在 npm 账号菜单中授权 GitHub：

- `@chunkdash/crossdesk-cli`
- `@chunkdash/crossdesk-cli-darwin-arm64`
- `@chunkdash/crossdesk-cli-darwin-x64`
- `@chunkdash/crossdesk-cli-linux-arm64`
- `@chunkdash/crossdesk-cli-linux-x64`
- `@chunkdash/crossdesk-cli-windows-x64`

六个包使用同一组配置：

| npm 字段 | 值 |
| --- | --- |
| Organization or user | `kunkundi`（GitHub 仓库所有者，不是 npm scope） |
| Repository | `crossdesk` |
| Workflow filename | `release-cli.yml`（只填写文件名） |
| Environment name | `npm` |
| Allowed actions | 勾选 **Allow npm publish**；本流程不需要 **Allow npm dist-tag** |

工作流使用 GitHub-hosted runner、Node.js 24 和 npm 12.2.1。GitHub 仓库的 `npm` environment 名称必须与 npm 配置完全一致，可在仓库 Settings → Environments 中限定只有 `cli-v*` 标签能部署；如设置此限制，分支预演也会被该环境规则拦截，应选择对应标签预演。可按团队需要设置 required reviewers；这是 GitHub 的发布审批，与 npm 2FA 不同。

新建 Trusted Publisher 后应在 **2 天内完成首次真实发布**，否则配置过期，需要删除后重建；预演不算成功发布。因此建议先推送工作流并完成预演，再创建 npm 信任配置，随后发布一个新版本。设置时 npm 可能要求当前账号完成浏览器 / 2FA 认证，不能由 GitHub Secrets 替代。若 scoped 入口包还没创建，需要先按下文从本地发布一次，再配置它的 Trusted Publisher。该绑定只解决身份认证，不豁免 npm 的包名与反垃圾审核。

### 构建缓存

CLI 构建分别缓存 Xmake 工具和已安装的原生依赖。原生依赖使用 `XMAKE_PKG_INSTALLDIR` 指定的 runner 临时目录，不缓存工程的 `.xmake` 配置或 `build` 产物。缓存 key 包含 runner 类型、目标架构、镜像版本、Xmake 版本以及构建配置、依赖配方和补丁的内容；不包含运行编号，也不因 CLI C++ 源码、README 或 npm 元数据修改而失效。镜像更新可能包含编译器／SDK 更新，因此会重新生成缓存；依赖配置变化时只接受新的完整 key，不回退使用旧配方的库。

`Restore native dependencies` 会显示缓存是否命中。各平台的 `xmake f` 成功安装依赖后立即执行 `Save native dependencies`，后续 CLI 编译、签名或打包失败也能在下一次复用；依赖安装失败时不会保存不完整缓存。Linux 仅对镜像中缺失的系统工具执行 APT 更新和安装。首次运行、缓存被清理、镜像或依赖配置变化仍需下载／编译依赖；缓存命中不代表跳过 CLI 自身构建和打包。

### 本地首次发布与故障恢复

首次使用 `@chunkdash` 前，确认当前 npm 账号属于该组织且有目标包的发布权限。本地发布需要已启用的 2FA 或符合 npm 要求的发布凭据；组织 owner 身份不代替发布认证。

```sh
npm whoami --registry=https://registry.npmjs.org/
npm org ls chunkdash --json --registry=https://registry.npmjs.org/
```

先重新构建五个平台的包；旧的 `crossdesk-cli-<平台>` tarball 不会自动变成 scoped 包，仅重命名文件也不会改变包内名称。已经发布的旧平台包保持原样，新入口只依赖 `@chunkdash` 包。每次版本发布同步更新 `apps/cli/package.json` 和 `apps/cli/xmake.lua`；若某个目标包的版本已经发布，不能用新的内容覆盖该版本。

如果五个 `@chunkdash/crossdesk-cli-*` 平台包的 `0.1.0` 已经发布，此次仅将入口从 `crossdesk-cli` 迁移到 `@chunkdash/crossdesk-cli`，可以沿用新入口的 `0.1.0` 并依赖原来的五个平台版本。确认注册表中各平台版本的完整性后，只发布新的 `chunkdash-crossdesk-cli-0.1.0.tgz`；不要重发已有平台版本。后续原生程序更新仍需整套提升版本并重新构建。

下载并解出同一提交、同一版本的六个 tarball，在其目录内执行发布。以下以 0.1.0 为例，先检查打包内容，再逐个发布五个平台包到 `next`：

```sh
(
  set -e
  for target in darwin-arm64 darwin-x64 linux-arm64 linux-x64 windows-x64; do
    npm publish "./chunkdash-crossdesk-cli-${target}-0.1.0.tgz" \
      --dry-run --ignore-scripts --access public --tag next --registry=https://registry.npmjs.org/
  done
  npm publish ./chunkdash-crossdesk-cli-0.1.0.tgz \
    --dry-run --ignore-scripts --access public --tag next --registry=https://registry.npmjs.org/
)
```

`--dry-run` 只做本地发布预演，不能证明包名会被注册表接受。检查通过后发布平台包：

```sh
(
  set -e
  for target in darwin-arm64 darwin-x64 linux-arm64 linux-x64 windows-x64; do
    npm publish "./chunkdash-crossdesk-cli-${target}-0.1.0.tgz" \
      --ignore-scripts --access public --tag next --registry=https://registry.npmjs.org/
  done
)
```

若 npm 返回正在处理（HTTP 202），等待对应的确切版本可查询，不要立即重复上传。遇到 `403 Package name triggered spam detection` 时停止，保留已成功的包，向 [npm 支持](https://www.npmjs.com/support)提供包名、仓库地址、版本及错误日志申请复核；不要自动重试整轮发布。恢复时先查询各包的确切版本，核对注册表的 `dist.integrity` 与 `npm pack <本地.tgz> --dry-run --json --ignore-scripts` 的 `integrity`，只补发未成功的包；同版本内容不同则停止并提升整套版本。

全部原生包可查询、完整性一致后，才发布通用入口：

```sh
(
  set -e
  for target in darwin-arm64 darwin-x64 linux-arm64 linux-x64 windows-x64; do
    npm view "@chunkdash/crossdesk-cli-${target}@0.1.0" version dist.integrity \
      --registry=https://registry.npmjs.org/
  done
  npm publish ./chunkdash-crossdesk-cli-0.1.0.tgz \
    --ignore-scripts --access public --tag next --registry=https://registry.npmjs.org/
)
```

再确认 `@chunkdash/crossdesk-cli@0.1.0` 的依赖和完整性，并在五个平台用 `npx @chunkdash/crossdesk-cli@0.1.0 doctor --json` 做安装验证；交互连接仍需实机验收。验收后按需将六个包的 `latest` 指向该版本。不要只凭 `--tag next` 假定首次发布的包不会出现 `latest`，应查询实际 `dist-tags`。缺失平台包时启动器明确报错，不下载别的平台程序或回退为源码编译。

依据：[npm bin / optionalDependencies / os / cpu](https://docs.npmjs.com/cli/v11/configuring-npm/package-json/)、[npm scopes](https://docs.npmjs.com/about-scopes/)、[npm publish](https://docs.npmjs.com/cli/v11/commands/npm-publish/)、[Trusted Publishing](https://docs.npmjs.com/trusted-publishers/)、[npx](https://docs.npmjs.com/cli/v11/commands/npx/)、[GitHub runner 平台](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)、[Windows Console 模式](https://learn.microsoft.com/en-us/windows/console/getconsolemode)。
