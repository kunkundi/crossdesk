# README 与使用指南截图 / Screenshot notes

[中文 README](../../README.md) · [English README](../../README_EN.md) · [中文使用指南](../USER_GUIDE.md) · [English user guide](../USER_GUIDE_EN.md)

## 来源 / Sources

- **2026-10-26** 从 `e551e04e` 的实际 Slint 组件重新生成中英文 README 引用的 6 张截图：ID 连接主窗口、IP 直连主窗口与六设备桌面墙，展示更新后的柔和阴影、输入框与按钮配色。沿用原有演示数据、图片尺寸及 2× 软件渲染。
- **2026-10-25** 删除“启用中继服务”开关，并将自托管配置拆分为普通标签、“编辑”按钮和开关后，从当前工作区组件重新生成常规设置与网络设置共 4 张中英文截图。
- **2026-10-25** 从 `ae6f81e8` 的实际 Slint 组件重新生成 16 张中英文截图：更新主窗口、常规设置、桌面墙设置、自托管及关于共 10 张，新增 IP 直连主界面、记住密码弹窗和网络设置共 6 张。使用 Slint 1.17.1、macOS arm64 和 2× 软件渲染。
- 捕获程序在**工程外临时目录**中复用现有 [`slint_ui_smoke_test`](../../apps/desktop/tests/slint_ui_smoke_test.cpp) 的截图辅助函数，并注入演示界面状态；没有启动应用运行时、打开监听端口或建立远控会话。没有修改产品组件或工程内测试，临时源码、可执行程序与 PPM 均位于工程外。
- 主窗口展示 ID / IP 模式切换、桌面墙入口和各自的近期设备；IP 模式展示地址编辑、六位密码和默认端口 52888，网络设置展示“启用 IP 直连”及关闭说明。IP 近期设备的 `LAN` 标记表示记录类型，不代表在线探测结果。
- 本机 ID `123 456 789`、密码 `Ab12Cd` / `Cd34Ef`、地址 `192.168.1.10` 与 `192.168.1.20–22`、设备名称和连接状态均为演示数据，不是真实设备凭据。IP 列表还包含 `192.168.2.10` 作为切换示例。截图中勾选的“记住密码”是演示选择，配置状态不代表全部运行时默认值。
- 桌面墙放大视图和会话画面设置共 4 张继续沿用 **2026-10-19** 从 `1ef372d2` 渲染的截图。桌面墙展示六台演示设备，主窗口展示三台。
- “关于”中的 `v1.5.2` 和“已是最新版本”是捕获工具的示例值，不代表当前发布版本或实际在线检查结果。会话图使用公开页面作为远端画面，展示真实工具栏与画面设置菜单；隐私屏按钮处于关闭状态。
- 缩略图沿用既有公开页面截图：`fixtures/source.png` 来自 [CrossDesk Xmake 源码页](https://github.com/kunkundi/crossdesk/blob/ios-support/xmake.lua)，`fixtures/website.png` 来自 [CrossDesk 官网](https://www.crossdesk.cn/)，两者采集于 2026-09-07。它们只用于展示预览效果，不代表这些页面正在对应设备上运行，页面中的版本信息也不代表最新发布。
- `web-client.png` 保留 **2026-09-25** 从 [Web 客户端](https://web.crossdesk.cn/) 采集的 1280 × 720 连接首页，页面版本为 `2026.09.24.2`，ID 与密码为空。本次未重新采集 Web 页面；桌面设备预览继续复用这张图片。

On **2026-10-26**, the six images referenced by the Chinese and English READMEs (ID mode, Direct IP mode, and the six-device DeskHub grid) were refreshed from the actual Slint components at `e551e04e`, showing the updated soft shadows and input and button colors. The existing demonstration data, image dimensions, and 2× software rendering were retained.

On **2026-10-25**, 16 images were rendered from the actual Slint components at `ae6f81e8`: ten refreshed images (main window, General settings, DeskHub settings, self-hosting, and About) and six additions (Direct IP mode, its password dialog, and Network settings), in Chinese and English. Later that day, the four General and Network settings images were refreshed from the working tree after removing Enable TURN Service and separating Self-Hosted Config into a label, Edit button, and switch. The capture-only program reused the existing helper functions in an external temporary directory, using Slint 1.17.1 on macOS arm64 at 2× scale. Capture did not modify product components or repository tests, and no listeners or remote sessions were opened. IDs, IPs, passwords, device records, connection states, and update results are illustrative; `LAN` labels indicate record type, not an online check. The checked Remember password box is a demonstration choice. The four DeskHub enlarged-view and session-video images remain from 2026-10-19, public-page fixtures from 2026-09-07, and the Web image from 2026-09-25. Native mobile screenshots are not included.

## 文件 / Files

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
| `web-client.png`（中文） | Web 连接首页 / Web connection page | 1280 × 720 |
| `cli-ansi-overview.png` | CLI ANSI 四象限全景 / ANSI quadrant overview | 1600 × 960 |
| `cli-kitty-overview.png` | CLI Kitty 协议像素全景 / Kitty protocol pixel overview | 1600 × 960 |

桌面图片使用 2× 原生渲染缩放，捕获窗口内容区，不包含 macOS 原生标题栏。未放大旧 UI 截图，也未重绘控件；演示远端画面由实际组件按比例显示。Markdown 只设置展示宽度。常规设置可滚动，`desktop-settings` 展示首屏，`desktop-settings-network` 展示网络连接分组；Windows 专属采集方式与便携版服务入口不会出现在这些 macOS 图片中。

<a id="cli-captures"></a>

## CLI 图示 / CLI captures

- **2026-10-31 04:39（UTC+8）重新采集**，使用在 macOS arm64 上从 `22796368` 构建的 release CLI，连接同一用户授权的被控设备，分别采集 ANSI 与 Kitty 输出。两次连接均保持只读；图示展示远端当前的动画窗口，替换此前的桌面壁纸画面。
- 使用工程外的 POSIX 伪终端采集器，视口为 **160 列 × 48 行**，每格 **10 × 20 像素**，输出 PNG 为 **1600 × 960**。顶栏与底部快捷键直接来自 CLI 输出；图示不包含连接密码或真实设备 ID。
- ANSI 图由实际转义序列恢复字符网格及前景／背景色，四象限字符按对应的色块渲染。Kitty 图由 CLI 内联发送的 zlib / Base64 RGB 帧解码，按协议 placement 的行列尺寸显示；采集器只提供内联图像能力，不支持共享内存。
- 这些图片是**实际 CLI 输出的渲染图**。Ghostty 和 macOS「终端」窗口的自动化访问被工具拒绝，未取得原生窗口截图，也未据此完成这两款应用的实窗验收。字体栅格、图像缩放和显示时序由采集器处理，不能用图中 FPS 推断 Ghostty 或其他终端的实际显示性能。
- 构建配置、采集脚本、原始输出与中间产物均存放在工程外临时目录；工程内只保存最终图示及文档。示例命令使用占位 ID，凭据通过隐藏提示输入。

Refreshed at **04:39 (UTC+8) on 2026-10-31** with a macOS arm64 release CLI built from `22796368`, using real read-only connections to the same authorized host. Both images now show the animation window on the remote desktop, replacing the previous wallpaper views. An external POSIX PTY collector reports 160 × 48 cells at 10 × 20 pixels and renders the actual CLI output to PNG. The ANSI image reconstructs character cells and colors; the Kitty image decodes inline RGB frames and applies their placement dimensions. The collector does not support shared-memory images. These are output renderings; native Ghostty and macOS Terminal window access was denied by the automation tool. They do not validate either application's window rendering or display performance. Passwords and real device IDs are omitted. Capture scripts, raw output, build configuration, and intermediate files stay outside the repository.

重新采集时，使用 release CLI，在支持相应协议的终端或 PTY 采集器中分别运行 `crossdesk-cli <设备ID> --renderer ansi --quality high` 和 `crossdesk-cli <设备ID> --renderer kitty --quality high`，通过隐藏提示输入密码，等待顶栏显示 `Displaying` 后捕获完整视口，再按 `q` 退出。若使用 PTY 采集器，须正确处理终端尺寸回复、ANSI 光标与颜色、Kitty 探测、分块上传和 placement 缩放，并在图注中明确采集方式。更新图片前检查敏感信息、画面比例和状态栏完整性，并同步更新本节的日期、版本与验证范围。

## 重新生成 / Regenerate

以下命令在已配置依赖的 macOS 工程根目录执行。生成程序只渲染演示状态；所有临时源码、可执行文件和 PPM 均存放在工程外，确认图片后才将 PNG 复制进 `docs/images`。`sips` 仅做无损格式转换，图片没有重绘或后期修改。

先检查 `/tmp/crossdesk-ui-capture-page` 和 `/tmp/crossdesk-ui-capture-language`：旧覆盖文件存在且非空时会优先于环境变量，需确认并移开后再捕获。`CROSSDESK_UI_CAPTURE_DEMO_ASSETS` 指向本目录，供近期设备缩略图使用。

### 生成捕获程序

复用现有源文件的捕获辅助函数，截去测试入口，在临时目录加入演示状态及独立入口；保持工程内测试文件不变。源码结构变化时需复核替换位置。

```bash
set -e
xmake b -y crossdesk_ui
capture_dir=$(mktemp -d "${TMPDIR:-/tmp}/crossdesk-readme-capture.XXXXXX")
export capture_dir
python3 - <<'PY'
from pathlib import Path
import os
root = Path.cwd()
out = Path(os.environ['capture_dir'])
source = (root / 'apps/desktop/tests/slint_ui_smoke_test.cpp').read_text()
source = source[:source.index('// Sample the native window while')]
old = '  // Preview DeskHub with the same demo images, without opening any sessions.'
new = '''  // Documentation-only state; no runtime, listeners, or remote sessions.
  window->set_local_password("Ab12Cd");
  window->set_ip_direct_enabled(true);
  window->set_ip_direct_enabled_draft(true);
  if (options.page == "settings-network") window->set_settings_open(true);
  if (options.page == "lan" || options.page == "lan-password") {
    window->set_lan_mode(true);
    window->set_lan_listener_ip("192.168.1.10");
    window->set_lan_listener_port("52888");
    window->set_lan_local_port("52888");
    window->set_lan_interfaces(std::make_shared<slint::VectorModel<slint::SharedString>>(
        std::vector<slint::SharedString>{"192.168.1.10", "192.168.2.10"}));
    window->set_lan_new_password("Cd34Ef");
    window->set_lan_listening(true);
    window->set_remote_id_input("192.168.1.20:52888");
    const auto recent = window->get_recent_connections();
    for (size_t i = 0; i < recent->row_count(); ++i) {
      auto device = recent->row_data(i).value();
      device.lan = true;
      device.online = false;
      device.remote_id = slint::SharedString(("lan-demo-" + std::to_string(i)).c_str());
      device.endpoint = slint::SharedString(("192.168.1." + std::to_string(20 + i) + ":52888").c_str());
      recent->set_row_data(i, device);
    }
    if (options.page == "lan-password") {
      window->set_lan_remote_address("192.168.1.20");
      window->set_lan_remote_port("52888");
      window->set_lan_open(true);
      window->set_lan_remember_password(true);
    }
  }
''' + old
assert source.count(old) == 1
source = source.replace(old,new)
old = '      // Let settings scroll animations settle as well as hover/focus feedback.'
new = '''      if (options.page == "settings-deskhub") window->set_settings_tab(1);
      if (options.page == "settings-network") {
        window->window().dispatch_pointer_scroll_event(
            slint::LogicalPosition({400.0f, 250.0f}), 0.0f, -148.0f);
        window->window().dispatch_pointer_move_event(slint::LogicalPosition({2.0f, 2.0f}));
      }
''' + old
assert source.count(old) == 1
source = source.replace(old,new)
# Preserve the six-device DeskHub composition used in the README.
old = 'if (connections.size() == 3 && options.page != "main-recent-overflow" &&'
new = 'if (connections.size() == 3 && !options.page.starts_with("gallery") && options.page != "main-recent-overflow" &&'
assert source.count(old) == 1
source = source.replace(old,new)
old = '    window->set_gallery_mode(true);'
assert source.count(old) == 1
source = source.replace(old,old + '\n    window->set_gallery_tile_width(300);')
old = '        698.0f;'
assert source.count(old) == 1
source = source.replace(old,'        options.page == "gallery" ? 500.0f : 698.0f;')
source += '''} // namespace
int main() {
  const auto options = LoadCaptureOptions();
  if (!options.enabled || options.snapshot_path.empty()) return 1;
  auto window = crossdesk::ui::MainWindow::create();
  auto stream = crossdesk::ui::StreamWindow::create();
  auto server = crossdesk::ui::ServerWindow::create();
  if (!RegisterFontAwesome(window->window()) || !RegisterFontAwesome(stream->window()) ||
      !RegisterFontAwesome(server->window())) return 2;
  window->set_local_id("123 456 789");
  window->set_local_password("Ab12Cd");
  window->window().set_size(slint::LogicalSize({640.0f, 472.0f}));
  return RunCaptureMode(options, window, stream, server);
}
'''
(out / 'readme_capture.cpp').write_text(source)
print(out / 'readme_capture.cpp')
PY
```

### 编译与捕获

以下路径对应 `macosx / arm64 / debug` 配置；其他配置先用 `xmake show -t slint_ui_smoke_test` 查看生成头文件与库路径，再调整。捕获按顺序执行，避免窗口互相抢占焦点。

```bash
slint_prefix=$(xmake lua -c 'import("core.project.config"); config.load(); import("core.project.project"); print(project.target("slint_ui_smoke_test"):pkg("slint"):installdir())')
xcrun clang++ -std=c++20 \
  -Iapps/desktop/src/gui \
  -Iapps/desktop/src/gui/assets/fonts \
  -Iapps/desktop/src/gui/assets/localization \
  -Ibuild/.gens/crossdesk_ui/macosx/arm64/debug/rules/slint \
  -isystem "$slint_prefix/include/slint" \
  "$capture_dir/readme_capture.cpp" \
  build/macosx/arm64/debug/libcrossdesk_ui.a \
  -L"$slint_prefix/lib" -lslint_cpp -Wl,-rpath,"$slint_prefix/lib" \
  -o "$capture_dir/readme_capture"

for locale in zh en; do
  language=0
  if [ "$locale" = en ]; then language=1; fi
  for page in main lan gallery; do
    image_page="$page"
    if [ "$page" = gallery ]; then image_page=deskhub; fi
    snapshot="$capture_dir/desktop-$image_page-$locale.ppm"
    SLINT_BACKEND=winit-software SLINT_SCALE_FACTOR=2 \
      CROSSDESK_UI_CAPTURE=1 \
      CROSSDESK_UI_CAPTURE_DEMO_ASSETS="$PWD/docs/images" \
      CROSSDESK_UI_CAPTURE_PAGE="$page" \
      CROSSDESK_UI_CAPTURE_LANGUAGE="$language" \
      CROSSDESK_UI_CAPTURE_SNAPSHOT="$snapshot" \
      "$capture_dir/readme_capture"
    sips -s format png "$snapshot" \
      --out "$capture_dir/desktop-$image_page-$locale.png"
  done
done
```

逐张检查语言、页面、文字裁切、尺寸和演示数据后，将这些 PNG 复制到 `docs/images`，并更新日期与源码版本。桌面墙设置的标签在窗口初始化后设置，网络页滚动到对应分组，避免捕获尚未稳定的页面。

### 其他页面

使用指南中的截图可用相同程序捕获，将循环中的页面列表换为 `lan-password settings settings-network settings-deskhub self-hosted about stream-video-settings`。桌面墙放大视图使用 `gallery-focus` 页面，输出文件命名为 `desktop-deskhub-focus-*`。生成器已为桌面墙保留六台演示设备、300px 磁贴宽度和 500px 网格窗口内容高度；放大视图保持 698px（macOS）。这些设置仅影响临时捕获程序的示例数据和窗口尺寸。

若更新 Web 图片，应打开 Web 客户端，等待表单与底部版本号加载完成，保持 ID 与密码为空，再截取页面视口；替换后也需重新生成桌面图以同步缩略图。
