# README 截图 / Screenshot notes

[中文 README](../../README.md) · [English README](../../README_EN.md)

## 来源 / Sources

- 桌面与 Web 首页截图生成日期：2026-09-25；`fixtures/source.png` 和 `fixtures/website.png` 保留 2026-09-07 的公开页面截图。
- 桌面 UI 源代码：`f73a57ba` 加本次工具栏关闭状态斜杠渲染修复，Slint 1.17.1，macOS arm64；本次扩展截图工具以捕获会话画面设置，并等待窗口完成尺寸与缩放初始化后导出。
- 桌面图片来自 [`slint_ui_smoke_test`](../../apps/desktop/tests/slint_ui_smoke_test.cpp) 的 UI 捕获模式，直接渲染仓库中的 Slint 组件；不是旧版本截图或重新绘制的界面。
- 主窗口展示“已连接服务器”和三台在线设备的完整示例状态。ID `123 456 789`、密码 `123456`、连接状态及设备记录均为演示数据，不是真实设备凭据或真实远程会话。示例设置不代表应用运行时默认值。
- 本次主窗口截图包含随机密码刷新按钮；设置截图包含强制中继与自动隐私屏选项；自托管弹窗仅显示服务器地址和信令端口。画质、帧率和画面偏好在会话工具栏的“画面设置”菜单中展示。
- 会话截图使用 `fixtures/website.png` 作为示例远端画面，展示真实工具栏和打开的画面设置菜单，不建立远程连接。“关于”截图中的 `v1.5.2` 和“已是最新版本”均为演示数据，不代表对线上最新版本的检查结果。
- 会话工具栏的隐私屏按钮展示“已关闭”状态。关闭标记使用直接绘制的斜线路径，避免软件渲染器将旋转矩形显示为竖线。
- 设备缩略图使用可公开的页面截图：`fixtures/source.png` 来自 [CrossDesk 的 Xmake 源码页](https://github.com/kunkundi/crossdesk/blob/ios-support/xmake.lua)，`fixtures/website.png` 来自 [CrossDesk 官网](https://www.crossdesk.cn/)，第三张复用 `web-client.png`。它们用于展示缩略图效果，不代表这些页面正在对应的远程电脑上运行。
- `web-client.png`：2026-09-25 在 [web.crossdesk.cn](https://web.crossdesk.cn/) 重新截取的连接首页，页面显示版本 `2026.09.24.2`，视口为 1280 × 720。ID 与密码输入框为空，没有发起远程连接；使用该图片作为设备缩略图的桌面截图也已重新生成。

Desktop images were refreshed on 2026-09-25 from UI source `f73a57ba`, using Slint 1.17.1 on macOS arm64. They render actual components with demonstration data, including the refreshed password controls, client settings, live video menu, and manual update check. The source-code and website fixtures remain public page captures from 2026-09-07. The Web connection page was recaptured on 2026-09-25 at 1280 × 720, showing version `2026.09.24.2` with empty credential fields; no remote connection was initiated. Desktop images using its thumbnail were regenerated as well. Desktop IDs, device names, settings, version numbers, and update results are illustrative. Native iOS screenshots are not included because this update did not capture a signed app running on a physical device.

The session images also include this change's toolbar slash-rendering fix. The privacy button shows the off state using diagonal paths that render correctly without rectangle rotation.

## 文件 / Files

| 文件 | 页面 | 语言 |
| --- | --- | --- |
| `desktop-main-zh.png` / `desktop-main-en.png` | Main window / 主窗口 | 中文 / English |
| `desktop-settings-zh.png` / `desktop-settings-en.png` | Settings / 设置 | 中文 / English |
| `desktop-self-hosted-zh.png` / `desktop-self-hosted-en.png` | Self-hosting / 自托管设置 | 中文 / English |
| `desktop-stream-video-settings-zh.png` / `desktop-stream-video-settings-en.png` | Session toolbar and video settings / 会话工具栏与画面设置 | 中文 / English |
| `desktop-about-zh.png` / `desktop-about-en.png` | About and manual update check / 关于与检查更新 | 中文 / English |
| `web-client.png` | Web connection page / Web 连接首页 | 中文 |

主窗口及其弹窗图片为 1280 × 900 PNG，会话图片为 1920 × 1200 PNG，均使用 2× 原生渲染缩放保持文字清晰，未放大旧图。Markdown 只设置展示宽度，保持原始比例。设置列表可滚动，截图仅展示首屏。演示配置显示硬件编解码器可用；实际可用性由平台、设备与构建决定。Windows 专属的采集方式与便携版服务入口不会出现在这些 macOS 截图中。

## 重新生成 / Regenerate

更新 Web 图片时，在浏览器中打开 [web.crossdesk.cn](https://web.crossdesk.cn/)，等待连接表单和底部版本号加载完成，保持 ID 与密码为空，截取页面视口。当前图片使用 1280 × 720 视口；浏览器导出的 JPEG 仅转换为 PNG，未修改页面布局或内容。替换 `web-client.png` 后，重新生成下方桌面截图以同步设备卡片缩略图，并更新本页的日期与页面版本号。

以下命令在仓库根目录、已完成依赖配置的 macOS 开发环境运行。`sips` 仅将 Slint 导出的 PPM 无损转换为 PNG，不修改 UI。其他平台可以使用支持 PPM 的图片格式转换工具。

```bash
set -e
xmake b -y slint_ui_smoke_test
mkdir -p build/readme-capture docs/images

for locale in zh en; do
  language=0
  if [ "$locale" = en ]; then language=1; fi
  for page in main settings self-hosted about stream-video-settings; do
    snapshot="$PWD/build/readme-capture/desktop-$page-$locale.ppm"
    SLINT_BACKEND=winit-software SLINT_SCALE_FACTOR=2 \
      CROSSDESK_UI_CAPTURE=1 \
      CROSSDESK_UI_CAPTURE_DEMO_ASSETS="$PWD/docs/images" \
      CROSSDESK_UI_CAPTURE_PAGE="$page" \
      CROSSDESK_UI_CAPTURE_LANGUAGE="$language" \
      CROSSDESK_UI_CAPTURE_SNAPSHOT="$snapshot" \
      xmake r slint_ui_smoke_test
    sips -s format png "$snapshot" \
      --out "docs/images/desktop-$page-$locale.png"
  done
done
```

`CROSSDESK_UI_CAPTURE_DEMO_ASSETS` 必须指向包含上述图片的目录。缺失图片会让捕获命令失败，避免再次导出空白缩略图；不设置该变量时，仍保留原有测试状态。捕获工具会先运行窗口事件循环，等待尺寸与缩放初始化；直接在 `show()` 后截图可能得到 1× 图片。

捕获模式兼容 `/tmp/crossdesk-ui-capture-page` 和 `/tmp/crossdesk-ui-capture-language` 两个旧覆盖文件；如存在，它们会优先于环境变量，运行前请检查。更新后逐张检查文字、布局、敏感信息和文档引用，并同步修改本页日期与源码版本。
