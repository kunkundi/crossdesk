# README 与使用指南截图 / Screenshot notes

[中文 README](../../README.md) · [English README](../../README_EN.md) · [中文使用指南](../USER_GUIDE.md) · [English user guide](../USER_GUIDE_EN.md)

## 来源 / Sources

- **2026-11-11** 从 `6414f231` 的实际 Slint 组件刷新 **22 张**中英文桌面截图，并新增 **4 张**双栏文件传输与反馈面板截图。使用 Slint 1.17.1、macOS arm64 和 2× 软件渲染；主窗口展示独立的 ID / IP 模式与桌面墙入口，设置展示最小化时暂停视频，会话菜单展示远端显示器分辨率。
- 捕获程序在**工程外临时目录**复用现有 [`slint_ui_smoke_test`](../../apps/desktop/tests/slint_ui_smoke_test.cpp) 的截图辅助函数，注入演示状态并直接渲染产品组件。没有启动应用运行时、监听端口或远控会话，也没有读取真实设备记录、浏览文件系统或执行传输。工程内未新增或修改测试；捕获源码、可执行文件和 PPM 均位于工程外。
- 本机 ID `123 456 789`、密码 `Ab12Cd` / `Cd34Ef`、地址 `192.168.1.10` 与 `192.168.1.20–22`、设备名称和连接状态均为演示数据。IP 列表还包含 `192.168.2.10`。主窗口展示三台设备，桌面墙展示六台；IP 近期设备的 `LAN` 标记表示记录类型，不代表在线探测结果。
- 文件传输图中的 `/Users/demo/Documents`、`/home/demo/Documents`、文件名、大小、日期与已完成任务均为演示数据。会话菜单中的分辨率列表也是演示能力，不表示某台真实显示器提供这些模式。勾选状态不代表全部运行时默认值。
- “关于”中的 `development` 是捕获程序设置的演示版本；未执行在线更新检查。反馈截图仅展示入口，没有导出本机日志或提交反馈。
- 缩略图和会话背景沿用公开页面截图：`fixtures/source.png` 来自 [CrossDesk Xmake 源码页](https://github.com/kunkundi/crossdesk/blob/ios-support/xmake.lua)，`fixtures/website.png` 来自 [CrossDesk 官网](https://www.crossdesk.cn/)，采集于 **2026-09-07**。它们只用于展示预览效果，不代表页面正在对应设备上运行，其中的版本和日期不是当前发布信息。
- `web-client.png` 沿用 **2026-09-25** 从 [Web 客户端](https://web.crossdesk.cn/) 采集的 1280 × 720 连接首页（页面版本 `2026.09.24.2`），ID 与密码为空。本轮没有重新采集 Web 页面、CLI 或原生移动端。CLI 图片保留 **2026-10-31** 的实际输出，来源见下方 [CLI 图示](#cli-captures)。

On **2026-11-11**, **22 desktop images** were refreshed and **4 file-transfer and feedback images** were added using the actual Slint components at `6414f231`, Slint 1.17.1 on macOS arm64, and 2× software rendering. The external capture program supplies demonstration data only: it starts no application runtime, listeners, remote sessions, file browsing, transfers, log export, or online update checks. No repository tests were added or changed. IDs, passwords, paths, file metadata, device states, resolution choices, and the `development` version are illustrative. Public-page fixtures remain from 2026-09-07, the Web image from 2026-09-25, and CLI output captures from 2026-10-31; native mobile screenshots are not included. Temporary source, binaries, and PPM files stay outside the repository.

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
| `desktop-files-*.png` | 双栏文件传输 / Dual-pane file transfer | 1920 × 1240 |
| `desktop-feedback-*.png` | 反馈与诊断日志入口 / Feedback and diagnostic log export | 1280 × 944 |
| `web-client.png`（中文） | Web 连接首页 / Web connection page | 1280 × 720 |
| `cli-ansi-overview.png` | CLI ANSI 四象限全景 / ANSI quadrant overview | 1280 × 800 |
| `cli-kitty-overview.png` | CLI Kitty 协议像素全景 / Kitty protocol pixel overview | 1280 × 800 |

桌面图片使用 2× 原生渲染缩放，捕获窗口内容区，不包含 macOS 原生标题栏。未放大旧 UI 截图，也未重绘控件；演示远端画面由实际组件按比例显示。Markdown 只设置展示宽度。常规设置可滚动，`desktop-settings` 展示首屏，`desktop-settings-network` 展示网络连接分组；Windows 专属采集方式与便携版服务入口不会出现在这些 macOS 图片中。

<a id="cli-captures"></a>

## CLI 图示 / CLI captures

- **2026-10-31 05:17–05:18（UTC+8）重新采集**，使用在 macOS arm64 上从 `1b716f36` 及当前工作区 ANSI 优化重新构建的 release CLI，连接同一用户授权的被控设备。两次连接均保持只读，展示远端播放动画时的不同画面。
- 沿用工程外的 POSIX 伪终端采集器，视口为 **128 列 × 40 行**，每格 **10 × 20 像素**，输出 PNG 为 **1280 × 800**。顶栏与底部快捷键直接来自 CLI 输出；图示不包含连接密码或真实设备 ID。
- ANSI 图由实际转义序列恢复字符网格及前景／背景色，四象限字符按对应色块渲染；采集器等待完整 ANSI 帧结束后保存，支持本轮新增的行内多片段差分。Kitty 图使用本地共享内存传输：采集器通过 `mmap` 复制完整 RGB 帧后返回确认，按协议 placement 的行列尺寸显示。采集时只读取状态栏所需的字符，保存图片时才做最终缩放和 PNG 渲染。
- 图中的 ANSI `FPS 26`、Kitty `FPS 24` 是对应统计区间的实际完整输出次数。会话日志确认本地输出预算均为 8MiB/s，Kitty 使用 `shared_memory`，未发生回退；两次连接正常退出。源分辨率与画面内容不同，不能用这两张图直接比较后端性能或量化本次优化收益。静止画面下 ANSI 差分为空时不产生新的输出计数，可显示 `FPS 0`。
- 这些图片是**实际 CLI 输出的渲染图**，本轮沿用 PTY 采集，没有取得 Ghostty 或 macOS「终端」的原生窗口截图。字体栅格、图像缩放和显示时序由采集器处理，不能用图中 FPS 推断终端的实际显示性能。
- 采集脚本、原始输出、构建日志、源码／二进制哈希和中间产物均存放在工程外临时目录；工程内只更新最终图示及来源说明。示例命令使用占位 ID，凭据通过隐藏提示输入；构建后已恢复原来的 debug 配置。

Refreshed at **05:17–05:18 (UTC+8) on 2026-10-31** with a freshly rebuilt macOS arm64 release CLI from `1b716f36` plus the working-tree ANSI optimizations, using real read-only connections to the same authorized host during animation playback. The external POSIX PTY collector retains 128 × 40 cells at 10 × 20 pixels and renders the actual CLI output to 1280 × 800 PNGs. The ANSI collector reconstructs character cells and colors, supports multiple changed spans per row, and waits for a complete frame before saving. The Kitty collector copies each complete shared-memory RGB frame through `mmap` before acknowledging it and applies placement dimensions when saving. The captured ANSI `FPS 26` and Kitty `FPS 24` are interval counts of complete outputs. Both sessions used an 8MiB/s local output budget and exited normally; Kitty used shared memory with no fallback. Source resolutions and scene content differ, so these images are not a direct backend comparison or an isolated optimization benchmark. An unchanged ANSI view can show `FPS 0`. These output renderings do not validate native Ghostty or macOS Terminal rendering or GPU performance. Passwords and real device IDs are omitted. Scripts, raw output, build logs, source/binary hashes, and intermediate files stay outside the repository; the original debug configuration was restored.

重新采集时，使用 release CLI，在支持相应协议的终端或 PTY 采集器中分别运行 `crossdesk-cli <设备ID> --renderer ansi --quality high` 和 `crossdesk-cli <设备ID> --renderer kitty --quality high`，通过隐藏提示输入密码，等待顶栏显示 `Displaying` 且统计稳定后捕获完整视口，再按 `q` 退出。若使用 PTY 采集器，须正确处理终端尺寸回复、ANSI 光标与颜色、Kitty 探测、分块上传、共享内存读取与确认，以及 placement 缩放，并在图注中明确采集方式。更新图片前检查敏感信息、画面比例和状态栏完整性，并同步更新本节的日期、版本与验证范围。

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
# Documentation-only additions for the current UI.
source = source.replace('window->set_current_version("v1.5.2");',
    'window->set_current_version("development");\n'
    '    window->set_update_check_status(crossdesk::ui::UpdateCheckStatus::Idle);')
source = source.replace('  // Preview DeskHub with the same demo images, without opening any sessions.',
    '  if (options.page == "feedback") window->set_feedback_open(true);\n'
    '  // Preview DeskHub with the same demo images, without opening any sessions.')
source = source.replace('      stream->set_video_menu_open(true);', r'''      stream->set_video_menu_open(true);
      stream->set_remote_resolution_visible(true);
      stream->set_remote_resolution_enabled(true);
      stream->set_remote_resolutions(std::make_shared<slint::VectorModel<slint::SharedString>>(
          std::vector<slint::SharedString>{"1280 × 720", "1920 × 1080", "2560 × 1440"}));
      stream->set_remote_resolution_index(1);''')
source = source.replace('      // Let settings scroll animations settle as well as hover/focus feedback.', r'''      if (options.page == "stream-video-settings") {
        stream->set_video_menu_open(false);
        const slint::LogicalPosition logo({19.0f, 19.0f});
        stream->window().dispatch_pointer_move_event(logo);
        stream->window().dispatch_pointer_press_event(logo, slint::PointerEventButton::Left);
        stream->window().dispatch_pointer_release_event(logo, slint::PointerEventButton::Left);
        stream->window().dispatch_pointer_move_event(slint::LogicalPosition({950.0f, 590.0f}));
        stream->set_video_menu_open(true);
      }
      // Let settings scroll animations settle as well as hover/focus feedback.''')
source = source.replace('options.page.rfind("settings", 0) == 0 ? 350 : 150',
    'options.page == "stream-video-settings" ? 650 : options.page.rfind("settings", 0) == 0 ? 350 : 150')
source = source.replace('int RunCaptureMode(', r'''int CaptureFileBrowser(const CaptureOptions &options) {
  auto files = crossdesk::ui::FileBrowserWindow::create();
  if (!RegisterFontAwesome(files->window())) return 2;
  const bool zh = options.language == 0;
  files->set_chinese(zh);
  files->set_peer_name("Office PC");
  files->set_local_role(zh ? "控制端" : "Controller");
  files->set_remote_role(zh ? "被控端" : "Host");
  files->set_local_path("/Users/demo/Documents");
  files->set_local_current_path("/Users/demo/Documents");
  files->set_remote_path("/home/demo/Documents");
  files->set_remote_current_path("/home/demo/Documents");
  files->set_local_up(true);
  files->set_remote_up(true);
  files->set_local_status(zh ? "4 个项目" : "4 items");
  files->set_remote_status(zh ? "4 个项目" : "4 items");
  std::vector<crossdesk::ui::BrowserEntry> local, remote;
  for (int pane = 0; pane < 2; ++pane) {
    const char *names[] = {"Design assets", "Release notes.md", "Roadmap.pdf", "Screenshots.zip"};
    const char *sizes[] = {"—", "12 KB", "248 KB", "8.4 MB"};
    auto &entries = pane == 0 ? local : remote;
    for (int i = 0; i < 4; ++i) {
      crossdesk::ui::BrowserEntry entry{};
      entry.name = names[i];
      entry.path = slint::SharedString((std::string(pane == 0 ? "/Users/demo/Documents/" : "/home/demo/Documents/") + names[i]).c_str());
      entry.directory = i == 0;
      entry.selected = pane == 0 && i == 3;
      entry.size = sizes[i];
      entry.modified = "2026-11-11 10:30";
      entries.push_back(entry);
    }
  }
  files->set_local_entries(std::make_shared<slint::VectorModel<crossdesk::ui::BrowserEntry>>(local));
  files->set_remote_entries(std::make_shared<slint::VectorModel<crossdesk::ui::BrowserEntry>>(remote));
  for (int pane = 0; pane < 2; ++pane) {
    std::vector<crossdesk::ui::BrowserTreeEntry> tree;
    const char *names[] = {"demo", "Desktop", "Documents", "Downloads"};
    for (int i = 0; i < 4; ++i) {
      crossdesk::ui::BrowserTreeEntry entry{};
      entry.name = names[i]; entry.depth = i == 0 ? 0 : 1;
      entry.path = slint::SharedString((std::string(pane == 0 ? "/Users/demo" : "/home/demo") + (i == 0 ? "" : std::string("/") + names[i])).c_str());
      entry.expanded = i == 0; entry.selected = i == 2;
      tree.push_back(entry);
    }
    auto model = std::make_shared<slint::VectorModel<crossdesk::ui::BrowserTreeEntry>>(tree);
    if (pane == 0) files->set_local_tree(model); else files->set_remote_tree(model);
  }
  files->set_local_selected(1);
  files->set_transfer_ready(true);
  files->set_can_upload(true);
  files->set_tasks_expanded(true);
  crossdesk::ui::BrowserTask task{};
  task.id = 1; task.name = "Roadmap.pdf"; task.size = "248 KB";
  task.detail = zh ? "发送到远端" : "Send to remote";
  task.status = zh ? "已完成" : "Completed";
  task.progress = 1.0f;
  files->set_tasks(std::make_shared<slint::VectorModel<crossdesk::ui::BrowserTask>>(std::vector{task}));
  files->set_task_summary(zh ? "1 个任务 · 已完成 1" : "1 task · 1 completed");
  files->window().set_size(slint::LogicalSize({960.0f, 620.0f}));
  files->show();
  bool saved = false;
  slint::Timer timer;
  timer.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(600), [&] {
    saved = WriteWindowSnapshot(files->window(), options.snapshot_path.c_str());
    slint::quit_event_loop();
  });
  slint::run_event_loop();
  return saved ? 0 : 7;
}

''' + 'int RunCaptureMode(')
source = source.replace('  auto window = crossdesk::ui::MainWindow::create();',
    '  if (options.page == "files") return CaptureFileBrowser(options);\n'
    '  auto window = crossdesk::ui::MainWindow::create();')
(out / 'readme_capture.cpp').write_text(source)
print(out / 'readme_capture.cpp')
PY
```

### 编译与捕获

macOS 编译需链接 `CoreFoundation`，供当前语言检测辅助函数使用。以下路径对应 `macosx / arm64 / debug` 配置；其他配置先用 `xmake show -t slint_ui_smoke_test` 查看生成头文件与库路径，再调整。捕获按顺序执行，避免窗口互相抢占焦点。

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
  -framework CoreFoundation \
  -o "$capture_dir/readme_capture"

for locale in zh en; do
  language=0
  if [ "$locale" = en ]; then language=1; fi
  for page in main lan gallery lan-password settings settings-network settings-deskhub self-hosted about gallery-focus stream-video-settings files feedback; do
    image_page="$page"
    if [ "$page" = gallery ]; then image_page=deskhub; fi
    if [ "$page" = gallery-focus ]; then image_page=deskhub-focus; fi
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

### 捕获范围

上面的循环生成本轮全部 26 张桌面图片。可缩短页面列表以仅刷新所需页面。生成器为桌面墙保留六台演示设备、300px 磁贴宽度和 500px 网格内容高度；放大视图保持 698px（macOS）。会话捕获先展开真实工具栏，再显示画面设置菜单；文件浏览窗口只填充内存中的示例行，不连接文件操作回调。这些设置仅影响临时捕获程序。

若更新 Web 图片，应打开 Web 客户端，等待表单与底部版本号加载完成，保持 ID 与密码为空，再截取页面视口；替换后也需重新生成桌面图以同步缩略图。
