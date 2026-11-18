# 更新文档配图

## 官网首页

用空白浏览器配置分别打开中英文官网，将窗口设为 1280 × 720 个逻辑像素。等首屏设备图加载完成，收起浏览器浮层，再截取带标签页和地址栏的完整窗口。中文版保存为 `fixtures/website.png`，英文版保存为 `fixtures/website-en.png`。[中文官网](https://www.crossdesk.cn/) · [English website](https://www.crossdesk.cn/en/)

Office PC 在近期设备和 DeskHub 中使用官网首页，会话画面设置也使用它作为背景。更新素材后，重新生成下方的桌面截图以同步这些画面。

## Web 客户端

更新 Web 首页时，使用空白浏览器配置打开线上客户端，将窗口设为 1280 × 720 个逻辑像素。等待表单和版本号显示，并确认设备 ID 与密码为空、连接状态提示已消失。截取包含标签页和地址栏的整个窗口，导出为 `web-client.png`。先更新捕获程序读取的这张图，再重新生成含近期设备或 DeskHub 磁贴的桌面截图。

## 桌面客户端

桌面墙的第二排依次展示 Studio Mac 的 Kitty 视频画面、Build Server 的构建终端和 Test Device 的 CPU 负载。构建终端素材 `fixtures/build-terminal.png` 可在运行 `xmake build crossdesk_wire` 后截取。CPU 曲线来自 macOS 活动监视器的「窗口 → CPU 历史记录」，将完整窗口分别保存为 `fixtures/cpu-history.png` 和 `fixtures/cpu-history-en.png`。更新素材后重新生成 `gallery` 和 `gallery-focus`。

以下命令在已配置构建依赖与 Python Pillow 的 macOS 工程根目录执行。采集时使用 2× 显示器，并允许终端录制屏幕。捕获源码、可执行文件和原始窗口图均保存在工程外临时目录。

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
for device, old_image, new_image in [
    ('Studio Mac', 'fixtures/source.png', 'cli-kitty-overview.png'),
    ('Build Server', 'fixtures/website.png', 'fixtures/build-terminal.png'),
    ('Test Device', 'web-client.png', 'fixtures/cpu-history.png'),
]:
    old = f'"{device}", "{old_image}"'
    assert source.count(old) == 1
    source = source.replace(old, f'"{device}", "{new_image}"')
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
    saved = CaptureTitledWindow(options.snapshot_path.c_str());
    slint::quit_event_loop();
  });
  slint::run_event_loop();
  return saved ? 0 : 7;
}

''' + 'int RunCaptureMode(')
source = source.replace('  auto window = crossdesk::ui::MainWindow::create();',
    '  if (options.page == "files") return CaptureFileBrowser(options);\n'
    '  auto window = crossdesk::ui::MainWindow::create();')
# Capture the native frame after the demo state has settled.
source = source.replace('namespace {', 'bool CaptureTitledWindow(const char *path);\n\nnamespace {', 1)
start = source.index('bool WriteWindowSnapshot(')
end = source.index('bool RegisterFontAwesome(', start)
source = source[:start] + source[end:]
source = source.replace('WriteWindowSnapshot(*capture_window, ', 'CaptureTitledWindow(')
(out / 'readme_capture.cpp').write_text(source)
print(out / 'readme_capture.cpp')
PY

cat > "$capture_dir/window_capture.mm" <<'OBJC'
#import <AppKit/AppKit.h>

bool CaptureTitledWindow(const char *path) {
  NSWindow *window = NSApp.keyWindow;
  if (!window || !window.visible || !(window.styleMask & NSWindowStyleMaskTitled))
    return false;
  NSTask *capture = [[NSTask alloc] init];
  capture.executableURL = [NSURL fileURLWithPath:@"/usr/sbin/screencapture"];
  capture.arguments = @[@"-x", @"-o", @"-l", @(window.windowNumber).stringValue,
                        [NSString stringWithUTF8String:path]];
  if (![capture launchAndReturnError:nil]) return false;
  [capture waitUntilExit];
  return capture.terminationStatus == 0;
}
OBJC
```

### 编译与捕获

使用 AppKit 获取当前窗口编号，将完整窗口保存为 PNG。以下路径对应 `macosx / arm64 / debug` 配置；其他配置先用 `xmake show -t slint_ui_smoke_test` 查看生成头文件与库路径，再调整。捕获按顺序执行，避免窗口互相抢占焦点。

```bash
slint_prefix=$(xmake lua -c 'import("core.project.config"); config.load(); import("core.project.project"); print(project.target("slint_ui_smoke_test"):pkg("slint"):installdir())')
xcrun clang++ -std=c++20 \
  -Iapps/desktop/src/gui \
  -Iapps/desktop/src/gui/assets/fonts \
  -Iapps/desktop/src/gui/assets/localization \
  -Ibuild/.gens/crossdesk_ui/macosx/arm64/debug/rules/slint \
  -isystem "$slint_prefix/include/slint" \
  "$capture_dir/readme_capture.cpp" "$capture_dir/window_capture.mm" \
  build/macosx/arm64/debug/libcrossdesk_ui.a \
  -L"$slint_prefix/lib" -lslint_cpp -Wl,-rpath,"$slint_prefix/lib" \
  -framework CoreFoundation -framework AppKit \
  -o "$capture_dir/readme_capture"

images="$PWD/docs/images"
english_assets="$capture_dir/assets-en"
mkdir -p "$capture_dir/raw" "$english_assets/fixtures"
ln -sf "$images/web-client.png" "$english_assets/web-client.png"
ln -sf "$images/cli-kitty-overview.png" "$english_assets/cli-kitty-overview.png"
ln -sf "$images/fixtures/cpu-history-en.png" "$english_assets/fixtures/cpu-history.png"
ln -sf "$images/fixtures/build-terminal.png" "$english_assets/fixtures/build-terminal.png"
ln -sf "$images/fixtures/source.png" "$english_assets/fixtures/source.png"
ln -sf "$images/fixtures/website-en.png" "$english_assets/fixtures/website.png"

for locale in zh en; do
  language=0
  assets="$images"
  if [ "$locale" = en ]; then
    language=1
    assets="$english_assets"
  fi
  for page in main lan gallery lan-password settings settings-network settings-deskhub self-hosted about gallery-focus stream-video-settings files feedback; do
    image_page="$page"
    if [ "$page" = gallery ]; then image_page=deskhub; fi
    if [ "$page" = gallery-focus ]; then image_page=deskhub-focus; fi
    snapshot="$capture_dir/raw/desktop-$image_page-$locale.png"
    SLINT_BACKEND=winit-software SLINT_SCALE_FACTOR=2 \
      CROSSDESK_UI_CAPTURE=1 \
      CROSSDESK_UI_CAPTURE_DEMO_ASSETS="$assets" \
      CROSSDESK_UI_CAPTURE_PAGE="$page" \
      CROSSDESK_UI_CAPTURE_LANGUAGE="$language" \
      CROSSDESK_UI_CAPTURE_SNAPSHOT="$snapshot" \
      "$capture_dir/readme_capture"
  done
done

python3 - "$capture_dir" <<'PY'
import io, sys
from pathlib import Path
from PIL import Image, ImageCms

out = Path(sys.argv[1])
for raw in (out / 'raw').glob('desktop-*.png'):
    with Image.open(raw) as window:
        rgb = ImageCms.profileToProfile(window.convert('RGB'),
            ImageCms.ImageCmsProfile(io.BytesIO(window.info['icc_profile'])),
            ImageCms.createProfile('sRGB'))
        background = Image.new('RGB', window.size, '#f1f2f3')
        background.paste(rgb, mask=window.convert('RGBA').getchannel('A'))
        background.save(out / raw.name)
PY
```

核对中英文窗口标题、缩略图和文字边缘后，将生成的 PNG 复制到 `docs/images`。

## CLI

在支持相应渲染模式的终端中分别运行以下命令，将示例 ID `123456789` 替换为被控电脑的 ID。按提示输入密码。等待顶栏显示 `Displaying` 后截取含状态栏和底部快捷键的完整视口。

```sh
crossdesk-cli 123456789 --renderer ansi --quality high
crossdesk-cli 123456789 --renderer kitty --quality high
```
