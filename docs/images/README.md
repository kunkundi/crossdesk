# 界面图片说明 / Screenshot notes

[中文首页](../../README.md) · [English home](../../README_EN.md) · [使用指南](../USER_GUIDE.md)

## 如何理解这些图片 / What the images show

桌面图片用于说明操作入口。设备 ID、密码、地址、设备名、文件路径、传输状态及分辨率选项均为演示数据，不能用于实际连接，也不代表你的设备默认设置。界面会随客户端版本、平台和语言有所不同。

Desktop images illustrate controls using demonstration IDs, passwords, addresses, device names, paths, transfer states, and resolution choices. They are not usable connection details or guaranteed defaults. Your interface may differ by version, platform, and language.

桌面图片在 **2026-11-11** 从实际界面组件生成，没有建立真实远控或文件传输。“关于”中的 `development` 是示例版本。会话背景来自 **2026-09-07** 的 [CrossDesk 官网](https://www.crossdesk.cn/) 和 [公开源码页面](https://github.com/kunkundi/crossdesk/blob/ios-support/xmake.lua)，不是当前发布信息。

Desktop images were generated from the actual UI components on **2026-11-11**, without live remote sessions or file transfers. The `development` version is illustrative. Background pages were captured on **2026-09-07** and do not indicate the current release.

Web 首页图采集于 **2026-09-25**，未填写 ID 或密码。当前图片不包含原生移动端截图。

The Web connection page was captured on **2026-09-25** with empty credentials. Native mobile screenshots are not included.

## 图片列表 / Images

| 文件（`zh` / `en`） | 页面 | PNG 尺寸 |
| --- | --- | --- |
| `desktop-main-*.png` | ID 连接模式 / ID mode | 1280 × 944 |
| `desktop-lan-*.png`（不含 `password`） | IP 直连模式 / Direct IP mode | 1280 × 944 |
| `desktop-lan-password-*.png` | IP 直连密码与记住密码 / Direct IP password dialog | 1280 × 944 |
| `desktop-settings-network-*.png` | IP 直连开关与网络设置 / Direct IP switch and Network settings | 1280 × 944 |
| `desktop-deskhub-*.png`（不含 `focus`） | 六设备桌面墙 / Six-device DeskHub grid | 1920 × 1000 |
| `desktop-deskhub-focus-*.png` | 桌面墙放大视图 / Enlarged DeskHub view | 1920 × 1396 |
| `desktop-settings-zh.png` / `desktop-settings-en.png` | 常规设置 / General settings | 1280 × 944 |
| `desktop-settings-deskhub-*.png` | 桌面墙传输设置 / DeskHub video settings | 1280 × 944 |
| `desktop-self-hosted-*.png` | 自托管设置 / Self-hosting | 1280 × 944 |
| `desktop-stream-video-settings-*.png` | 会话工具栏与画面设置 / Session video settings | 1920 × 1200 |
| `desktop-about-*.png` | 关于与检查更新 / About and update check | 1280 × 944 |
| `desktop-files-*.png` | 双栏文件传输 / Dual-pane file transfer | 1920 × 1240 |
| `desktop-feedback-*.png` | 反馈与诊断日志入口 / Feedback and diagnostic log export | 1280 × 944 |
| `web-client.png`（中文） | Web 连接首页 / Web connection page | 1280 × 720 |
| `cli-ansi-overview.png` | CLI ANSI 四象限全景 / ANSI quadrant overview | 1280 × 800 |
| `cli-kitty-overview.png` | CLI Kitty 协议像素全景 / Kitty protocol pixel overview | 1280 × 800 |

<a id="cli-captures"></a>

## CLI 画面 / CLI output

CLI 两图采集于 **2026-10-31**，来自同一授权设备的实际只读连接。图片按 128 列 × 40 行还原终端的 ANSI / Kitty 输出，不是终端窗口截图。实际显示受终端协议、字体、行距和窗口大小影响。

图中 FPS 是当时的输出计数，静止 ANSI 画面可以为 0；两图不能直接用于性能比较，也不承诺你的设备达到相同帧率。

The CLI images reconstruct ANSI / Kitty output from real read-only connections to the same authorized device on **2026-10-31**, using a 128 × 40 character viewport. They are output renders, not terminal-window screenshots. Appearance depends on the terminal, font, spacing, and window size. FPS values describe those captures, not guaranteed performance; an unchanged ANSI view can report 0 FPS.
