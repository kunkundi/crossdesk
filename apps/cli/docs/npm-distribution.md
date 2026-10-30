# npm 分发与平台兼容性

## 使用方式

完整版本发布后的统一入口如下。构建产物或部分平台包上传成功，不代表入口包和全部平台依赖已经可用；发布者须完成本文的注册表检查。

```sh
npx crossdesk-cli@latest doctor --json
npx crossdesk-cli@latest 123456789
npm install -g crossdesk-cli
crossdesk-cli 123456789
```

需要 Node.js 22 或以上和 npm。启动器根据 Node 的 `process.platform` / `process.arch` 选择同版本的 `@chunkdash` 平台包，直接启动原生 C++ 程序，继承终端、工作目录、环境变量和命令参数。安装过程不下载外部可执行文件、不执行 postinstall、不在用户机器编译。不要使用 `--omit=optional`，否则对应平台包不会安装。

平台包仅提供原生二进制，不声明 `bin` 命令。`npm install @chunkdash/crossdesk-cli-linux-x64` 不会让 shell 出现 `crossdesk-cli`；普通用户应安装上面的通用入口。入口包的本地安装将命令放在 `node_modules/.bin`，全局安装才会放入 npm 的全局命令目录。

## 平台与终端

| 平台 | 原生包 | 终端适配与运行边界 |
| --- | --- | --- |
| macOS Apple Silicon | `@chunkdash/crossdesk-cli-darwin-arm64` | POSIX TTY；具体最低系统版本写入包内 `build-info.json` |
| macOS Intel | `@chunkdash/crossdesk-cli-darwin-x64` | POSIX TTY；最低系统版本同上 |
| Linux x64 | `@chunkdash/crossdesk-cli-linux-x64` | glibc、libstdc++、zlib；所需 GLIBC / GLIBCXX 版本写入 `build-info.json` |
| Linux arm64 | `@chunkdash/crossdesk-cli-linux-arm64` | 同上 |
| Windows x64 | `@chunkdash/crossdesk-cli-windows-x64` | Windows 10 或以上，原生 Console / ConPTY 与 VT 输出 |

`apps/cli/npm/platforms.json` 的 `packageName` 是打包脚本、入口包依赖和启动器共同使用的名称来源。Windows 的内部目标键仍为 `win32-x64`，包内 `os` 仍为 `win32`；只将 npm 包名写为 `windows-x64`，不能把 Node 平台标识也改成 `windows`。Scope 用于明确组织归属，不保证免于 npm 的名称或反垃圾审核。

这些是构建与分发目标，不代表均已实机通过。当前完整构建与 npm 运行验证只覆盖 macOS arm64；CLI 主程序与平台 I/O 已通过 Windows x64 与 Linux x64、arm64 的交叉编译检查。其他平台的完整 MiniRTC 链接、实际视频连接、键鼠、终端恢复仍需对应机器验收。

普通终端使用 ANSI 渲染，按能力选择 TrueColor / 256 色 / 基础色；Kitty 图形协议是可选增强。macOS Terminal、iTerm2、Linux 常用终端、Windows Terminal 和 VS Code 集成终端属于目标场景。SSH 应分配 PTY（`ssh -t`）；tmux / screen 使用 ANSI。不支持 `TERM=dumb` 或把交互观看输出重定向到文件；非交互宿主使用 `bridge --stdio`。Alpine / musl 当前没有预编译包。Windows 独立 mintty 窗口可能不提供 Console / ConPTY，应使用 Windows Terminal 承载 Git Bash。

CI 的 macOS 构建配置指定 `--target_minver=13.0`，但最终必须检查实际产物及依赖。当前开发机的本地二进制最低要求为 **macOS 27.0**，不应将这个本地产物宣称为旧 macOS 通用版。

## 生成包

先按 [构建说明](terminal-client.md) 构建 release。macOS 打包前需要签名，CI 使用 ad-hoc 签名；本地可执行以下命令（Intel 将 `arm64` 换成 `x86_64`）：

```sh
codesign --force --sign - build/macosx/arm64/release/crossdesk-cli
```

然后生成 npm 包：

```sh
npm --prefix apps/cli run pack:npm
```

脚本读取 `apps/cli/package.json` 与 `apps/cli/xmake.lua` 的版本，检查二者一致；确认源码中的 MiniRTC 日志脱敏补丁已应用，再检查当前平台二进制格式、CPU、动态链接依赖，并运行离线 `doctor --json`。macOS 只接受系统动态库，Linux 只接受列明的基础系统运行库，Windows 拒绝未随产品处理的 DLL 和动态 MSVC 运行库。Windows 在含 `dumpbin.exe` 的 Visual Studio 开发者环境中运行打包。脚本预先检查两个输出文件是否存在，并在临时目录中完成两个包后再写入输出目录；写入失败时清理本次已写入的包，已有同名产物不会被覆盖。

默认从 `build/<platform>/<arch>/release/` 读取原生程序，将两个 `.tgz` 写入 `build/npm/`。可指定路径：

```sh
npm --prefix apps/cli run pack:npm -- --binary /absolute/path/crossdesk-cli --out-dir /absolute/path/output
```

每台构建机生成当前平台的原生包与相同版本的通用启动包。以 macOS arm64、版本 0.1.0 为例，文件名为 `chunkdash-crossdesk-cli-darwin-arm64-0.1.0.tgz` 和 `crossdesk-cli-0.1.0.tgz`；npm 将 `@scope/name` 的 tarball 文件名写作 `scope-name-version.tgz`。五个 CI artifact 各含一份入口包，最终需要五个平台包和一份入口包，共六个 tarball。

`apps/cli/npm/platforms.json` 集中定义五个平台；脚本校验平台包名合法且不重复，启动包通过精确版本的 `optionalDependencies` 让 npm 仅安装匹配系统和 CPU 的原生包。生成的六个包都包含作者、源码仓库、主页、问题反馈地址，以及面向 npm 官方注册表的公开发布配置。原生包包含二进制、运行要求、SHA-256，以及当前控制端和已应用修补的 MiniRTC 源码快照。外部 Xmake 依赖的许可证、对应源码和重链接材料需要随正式版本一并提供，参见 [源码来源](source-provenance.md)。源码快照已含 MiniRTC，解包后无需运行 `prepare-deps.sh`；在 `source/` 目录按上述构建说明显式关闭桌面、开启 CLI，再构建 `crossdesk-cli`。源码包仅包含 CLI 及其依赖，不包含桌面或移动应用。

`apps/cli/package.json` 的 `private: true` 表示源码目录不是发布产物。只发布打包脚本生成的 tarball；生成的包不带 private 标记。

## 本地安装验证

以 macOS arm64 为例，用生成包的绝对路径，在工程外的临时目录执行：

```sh
npx --yes --offline \
  --package /absolute/path/chunkdash-crossdesk-cli-darwin-arm64-0.1.0.tgz \
  --package /absolute/path/crossdesk-cli-0.1.0.tgz \
  -- crossdesk-cli doctor --json
```

也可先 `npm install --ignore-scripts` 安装这两个本地 tarball，再使用安装目录中的 `crossdesk-cli`。验证交互观看需要真实 TTY 与授权的被控端，doctor 不会建立远程连接。

Pi 等宿主应直接启动原生程序以可靠控制进程和专用密码描述符。全局安装后，用 `crossdesk-cli --print-binary-path` 获取路径，再将该路径设为 `CROSSDESK_CLI`；不要把整条 npx 命令放进此变量。

## CI 与发布

`.github/workflows/build-cli.yml` 配置五个平台的原生 release 构建、doctor 和打包，并上传 tarball；该工作流不执行 npm publish。更新 MiniRTC 引用时，必须先将对应提交推送到子模块远端，再推送 CrossDesk，否则全新检出和 CI 会在拉取子模块时失败。

首次使用 `@chunkdash` 前，确认当前 npm 账号属于该组织且有目标包的发布权限。本地发布需要已启用的 2FA 或符合 npm 要求的发布凭据；组织 owner 身份不代替发布认证。

```sh
npm whoami --registry=https://registry.npmjs.org/
npm org ls chunkdash --json --registry=https://registry.npmjs.org/
```

先重新构建五个平台的包；旧的 `crossdesk-cli-<平台>` tarball 不会自动变成 scoped 包，仅重命名文件也不会改变包内名称。已经发布的旧平台包保持原样，新入口只依赖 `@chunkdash` 包。每次版本发布同步更新 `apps/cli/package.json` 和 `apps/cli/xmake.lua`；若某个目标包的版本已经发布，不能用新的内容覆盖该版本。

下载并解出同一提交、同一版本的六个 tarball，在其目录内执行发布。以下以 0.1.0 为例，先检查打包内容，再逐个发布五个平台包到 `next`：

```sh
(
  set -e
  for target in darwin-arm64 darwin-x64 linux-arm64 linux-x64 windows-x64; do
    npm publish "./chunkdash-crossdesk-cli-${target}-0.1.0.tgz" \
      --dry-run --ignore-scripts --access public --tag next --registry=https://registry.npmjs.org/
  done
  npm publish ./crossdesk-cli-0.1.0.tgz \
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
  npm publish ./crossdesk-cli-0.1.0.tgz \
    --ignore-scripts --access public --tag next --registry=https://registry.npmjs.org/
)
```

再确认 `crossdesk-cli@0.1.0` 的依赖和完整性，并在五个平台用 `npx crossdesk-cli@0.1.0 doctor --json` 做安装验证；交互连接仍需实机验收。验收后按需将六个包的 `latest` 指向该版本。不要只凭 `--tag next` 假定首次发布的包不会出现 `latest`，应查询实际 `dist-tags`。缺失平台包时启动器明确报错，不下载别的平台程序或回退为源码编译。

后续接入 GitHub Actions Trusted Publishing 时，需要在入口包和五个 scoped 平台包各自的 npm Settings 中配置同一发布工作流；它解决发布身份认证，不豁免包名审核。当前构建工作流仍只生成 artifact，不会因为采用 scope 而自动发布。

依据：[npm bin / optionalDependencies / os / cpu](https://docs.npmjs.com/cli/v11/configuring-npm/package-json/)、[npm scopes](https://docs.npmjs.com/about-scopes/)、[npm publish](https://docs.npmjs.com/cli/v11/commands/npm-publish/)、[Trusted Publishing](https://docs.npmjs.com/trusted-publishers/)、[npx](https://docs.npmjs.com/cli/v11/commands/npx/)、[GitHub runner 平台](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)、[Windows Console 模式](https://learn.microsoft.com/en-us/windows/console/getconsolemode)。
