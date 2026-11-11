# CrossDesk Sidebar for DeepSeek Harness

在 DeepSeek Harness **桌面版**中，自动将 CrossDesk MCP 返回的页面打开到发起调用的会话侧栏。适配 Harness 0.2.2-rc.1 的 Sidebar Browser 和 Connection 接口。浏览器版的 iframe 不在支持范围内。

## 安装

先按 [DSH 完整安装指南](../../README.md#install-dsh) 安装 CrossDesk MCP 和兼容的原生 CLI，并注册名为 `crossdesk` 的 MCP 服务。**本插件不会自动注册 MCP 服务**；服务配置和插件安装缺一不可。

使用 MCP 本地安装包时，插件及构建产物已包含在包内：

```sh
dsh plugin --profile desktop add /absolute/path/crossdesk-mcp/node_modules/@chunkdash/crossdesk-mcp/integrations/deepseek-harness
```

从源码运行时，先构建 MCP，再安装工程中的插件目录：

```sh
npm --prefix /absolute/path/crossdesk/apps/mcp run build
dsh plugin --profile desktop add /absolute/path/crossdesk/apps/mcp/integrations/deepseek-harness
```

也可在 Harness 的插件页面通过本地路径安装。重启 Harness 和 CrossDesk MCP 后，确认 `crossdesk-sidebar` 组件启用并运行。MCP 的 YAML 配置、源码运行所需的 `--native-binary`、首次连接及排查步骤均见上述完整指南。该包只增加侧栏适配，不连接设备、不修改设备授权。

不要强制使用 `--panel=apps` 或 `--browser-launch=system`；保留默认 `auto`，或显式使用 `--panel=browser --browser-launch=host`。若服务名称不同，在 Harness profile 的 `cordis.patch.yml` 中添加：

```yaml
- id: crossdesk-sidebar
  config:
    serverName: your_crossdesk_server_name
```

## 行为与边界

- 宿主插件只接收指定 MCP 名称的 `crossdesk_open`、`crossdesk_connect`、`crossdesk_open_browser`、`crossdesk_unlock` 成功结果，验证页面类型及 `127.0.0.1` 启动地址。
- 通过 Harness 已认证的 Connection 接口交接页面请求；客户端约每 750 毫秒检查当前会话，只在发起调用的会话中打开侧栏，不读取聊天记录或重新执行工具。
- 页面请求在内存中最多保存 60 秒，具有多窗口领取租约及确认机制。相同视图重新签发时替换插件打开的旧标签；切换会话、卸载、过期后不重放历史启动地址。
- 地址仅用于导航，不抓取启动页、复制配对令牌或代理视频。CrossDesk 继续使用本机 WebSocket / WebCodecs 播放和现有设备授权；插件不声明页面已经配对或视频已就绪，Agent 仍通过 `crossdesk_get_session` 确认。
- Harness 网页版未启用此桥接，因为其 iframe 不能嵌入受保护的 CrossDesk 页面。不会放宽 CrossDesk 的 Origin、CSP、Cookie 或 `X-Frame-Options` 策略。

该插件与 MCP 一起构建，产物位于本目录的 `dist/`。卸载使用 `dsh plugin --profile desktop remove @chunkdash/dsh-crossdesk-sidebar`。

2026-11-12 已在 macOS / DeepSeek Harness 0.2.2-rc.1 验证自动打开、配对及 1920 × 1080、约 27–29 FPS 的实时播放；远端为已授权的 Mac mini，仅查看模式。

宿主接口参考：[Sidebar Browser](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/client/ui-sidebar-browser)、[Right Sidebar](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/client/ui-sidebar-right)、[Connection](https://github.com/deepseek-ai/deepseek-harness/tree/master/packages/client/connection)。
