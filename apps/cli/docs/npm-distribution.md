# CLI 安装、更新与平台兼容性

[返回 CLI 指南](../README.md) · [终端操作](terminal-client.md)

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
