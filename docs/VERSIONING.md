# 查看版本与匹配源码

[返回首页](../README.md) · [自行构建](BUILD.md)

## 查看已安装版本

桌面端打开 **菜单 → 关于**，移动端打开 **设置 → 关于**。反馈问题时同时提供控制端和被控端的版本；使用 CLI 或 MCP 时，还应提供工具包版本。

下载页面的版本、应用显示的版本和仓库当前源码不一定相同。需要复现某个安装包时，优先使用关于页或开源信息中记录的源码标签、提交，而不是直接下载最新分支。

仅构建日期变化不会触发桌面升级提示，更新比较使用数字版本和补丁号。更新方式见 [更新指南](UPDATER.md)。

## 查看源码版本

在仓库根目录执行：

```sh
python3 scripts/version.py show
python3 scripts/version.py show client
python3 scripts/version.py show mcp
python3 scripts/version.py show --json
python3 scripts/version.py check
```

`show` 读取本地源码中的版本；`check` 检查本地版本是否一致。它们不会查询 npm、应用商店或下载服务器，不能据此判断某个版本是否已发布。

| 版本组 | 包含内容 |
| --- | --- |
| `client` | Windows、macOS、Linux、Android、iOS、HarmonyOS 客户端；移动端各自保留构建号 |
| `cli` / `mcp` | CLI、MCP、DSH 插件及 MCP 所需的原生程序，需保持一致 |
| `pi` | Pi 扩展，独立编号 |

MCP 出现原生程序版本不匹配时，优先重新安装同版本的完整平台包；源码安装则重新构建同一份源码中的 CLI 和 MCP。

## 为自己的构建设置版本（可选）

普通安装和更新无需修改源码版本。需要给自己的构建编号时，可以先预览修改，再应用：

```sh
python3 scripts/version.py set client 1.7.7 --dry-run
python3 scripts/version.py set client 1.7.7
python3 scripts/version.py check
```

`1.7.7` 是示例，替换为自己的版本。参数使用 `X.Y.Z`，不包含标签前缀 `v`、日期或构建元数据。CLI、MCP 和 Pi 也接受 `X.Y.Z-rc.N` 等预发布版本。

修改任一客户端版本会同步全部客户端，并按需要递增移动端构建号。修改 `cli` 或 `mcp` 任一项都会同步整个工具组；只需执行其中一条。例如：

```sh
python3 scripts/version.py set mcp 0.3.0 --dry-run
python3 scripts/version.py bump client patch --dry-run
python3 scripts/version.py bump android build --dry-run
```

移除 `--dry-run` 才会写入文件。`bump android build` 仅递增 Android 构建号；`major`、`minor`、`patch` 分别递增对应版本段。脚本不创建 Git 标签、不提交代码，也不自动构建或发布。

已有 Xmake 配置可能保留旧版本值，修改后请重新配置。打包时让程序版本和包名一致，具体命令见 [构建指南](BUILD.md)。
