# 在 DeepSeek Harness 中使用 CrossDesk

安装 CrossDesk 插件后，你可以让 Agent 操作远程电脑，并在当前对话的右侧浏览器监看画面。

## 安装

1. 按 [下载安装包](../../README.md#install-package) 选择与你的本机系统和芯片匹配的平台包，解压 ZIP 并取出 `.tgz` 文件。
2. 在 Harness 桌面版的插件页面安装该文件，或执行以下命令。将示例中的路径和版本号替换为实际文件：

   ```sh
   dsh plugin --profile desktop add "/absolute/path/chunkdash-crossdesk-mcp-0.1.0.tgz"
   ```

3. 重启 Harness，确认 CrossDesk 插件已启用。

平台包已经包含所需的原生程序，并自动提供 MCP 工具和侧栏画面。安装时需要联网下载依赖。终端找不到 `dsh` 时，可直接使用插件页面，或按 [完整安装指南](../../README.md#install-dsh) 使用应用自带的命令。

## 连接并使用

在对话中发送：

> 打开 CrossDesk，让我选择远程电脑。

在右侧面板输入远端设备 ID 和访问密码。需要 Agent 操作远端时选择 **允许 Agent 操作**，然后点击 **授权并连接**。看到远端桌面后，即可下达任务，例如：

> 在远程电脑的浏览器中搜索天气。

密码只在面板中输入。需要停止操作时点击 **暂停 Agent**；结束使用时，在 **会话选项** 中选择 **断开连接**。关闭侧栏本身不会断开远端连接。

保存设备、自动连接、人工接管和声音的用法见 [用户指南](../../README.md#用户连接流程)。如果没有工具或画面，请查看 [常见问题](../../README.md#install-troubleshooting)。

## 更新与卸载

通过插件页面安装新版平台包后重启 Harness。卸载也可在插件页面完成，或执行：

```sh
dsh plugin --profile desktop remove @chunkdash/crossdesk-mcp
```

卸载后，保存在本机的设备配置和系统凭据仍会保留。

源码构建和集成机制见 [开发参考](../../DEVELOPMENT.md#harness-development)。
