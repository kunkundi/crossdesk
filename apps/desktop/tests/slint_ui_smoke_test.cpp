#include "crossdesk_ui.h"
#include "fa_solid_900.h"
#include "features/gallery/gallery_window_state.h"
#include "ui/release_notes_text.h"
#include "ui/ui_localization.h"

#include <slint-platform.h>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

// Set the CROSSDESK_*_UI_SNAPSHOT variables while using Slint's software
// renderer to export deterministic active and inactive visual-check images.
bool WriteWindowSnapshot(const slint::Window &window, const char *path) {
  const auto snapshot = window.take_snapshot();
  if (!snapshot) {
    return false;
  }

  std::ofstream output(path, std::ios::binary);
  output << "P6\n"
         << snapshot->width() << ' ' << snapshot->height() << "\n255\n";
  for (const auto &pixel : *snapshot) {
    output.put(static_cast<char>(pixel.r));
    output.put(static_cast<char>(pixel.g));
    output.put(static_cast<char>(pixel.b));
  }
  return output.good();
}

bool RegisterFontAwesome(slint::Window &window) {
  return !window.window_handle()
              .register_font_from_data(fa_solid_900_ttf,
                                       fa_solid_900_ttf_len)
              .has_value();
}

struct CaptureOptions {
  bool enabled = false;
  std::string page;
  int language = 0;
  std::string snapshot_path;
  std::string demo_assets_path;
};

// Compatibility entry point for the former application-level debug capture
// mode. CROSSDESK_UI_CAPTURE keeps the interactive workflow, while
// CROSSDESK_UI_CAPTURE_SNAPSHOT writes a deterministic PPM and exits so every
// page can be exercised in automation. The legacy /tmp overrides still win.
std::string ReadFirstLine(const char *path) {
  std::ifstream input(path);
  std::string value;
  std::getline(input, value);
  return value;
}

CaptureOptions LoadCaptureOptions() {
  CaptureOptions options;
  if (const char *capture = std::getenv("CROSSDESK_UI_CAPTURE")) {
    options.enabled = std::string_view(capture) != "0";
  }
  if (!options.enabled) {
    return options;
  }

  if (const char *page = std::getenv("CROSSDESK_UI_CAPTURE_PAGE")) {
    options.page = page;
  }
  if (const std::string page_override =
          ReadFirstLine("/tmp/crossdesk-ui-capture-page");
      !page_override.empty()) {
    options.page = page_override;
  }

  std::string language_value =
      ReadFirstLine("/tmp/crossdesk-ui-capture-language");
  if (language_value.empty()) {
    if (const char *language =
            std::getenv("CROSSDESK_UI_CAPTURE_LANGUAGE")) {
      language_value = language;
    }
  }
  if (!language_value.empty()) {
    int language = 0;
    const auto [end, error] = std::from_chars(
        language_value.data(), language_value.data() + language_value.size(),
        language);
    if (error == std::errc{} &&
        end == language_value.data() + language_value.size()) {
      options.language =
          crossdesk::localization::detail::ClampLanguageIndex(language);
    }
  }
  if (const char *snapshot =
          std::getenv("CROSSDESK_UI_CAPTURE_SNAPSHOT")) {
    options.snapshot_path = snapshot;
  }
  if (const char *assets = std::getenv("CROSSDESK_UI_CAPTURE_DEMO_ASSETS")) {
    options.demo_assets_path = assets;
  }
  return options;
}

// Opt-in documentation data, separate from the smoke test's baseline state.
// The images are public page captures, never a user's cached remote desktops.
bool ConfigureDocumentationDemo(
    const slint::ComponentHandle<crossdesk::ui::MainWindow> &window,
    const CaptureOptions &options) {
  if (options.demo_assets_path.empty()) {
    return true;
  }
  struct DemoDevice {
    const char *id;
    const char *name;
    const char *thumbnail;
  };
  const DemoDevice devices[] = {
      {"987654321", "Dev Workstation", "fixtures/source.png"},
      {"246813579", "Office PC", "fixtures/website.png"},
      {"135792468", "Home Laptop", "web-client.png"},
      {"864209753", "Studio Mac", "fixtures/source.png"},
      {"753198642", "Build Server", "fixtures/website.png"},
      {"642087531", "Test Device", "web-client.png"},
  };
  std::vector<crossdesk::ui::RecentConnection> connections;
  for (const auto &device : devices) {
    if (connections.size() == 3 && options.page != "main-recent-overflow" &&
        options.page != "main-recent-scrolled") {
      break;
    }
    const std::string path = options.demo_assets_path + "/" + device.thumbnail;
    const auto thumbnail =
        slint::Image::load_from_path(slint::SharedString(path.c_str()));
    if (thumbnail.size().width == 0 || thumbnail.size().height == 0) {
      std::cerr << "Missing documentation thumbnail: " << path << '\n';
      return false;
    }
    crossdesk::ui::RecentConnection connection{};
    connection.remote_id = device.id;
    connection.display_name = device.name;
    connection.host_name = device.name;
    connection.online = true;
    connection.thumbnail = thumbnail;
    connection.has_thumbnail = true;
    connections.emplace_back(std::move(connection));
  }
  window->set_recent_connections(
      std::make_shared<slint::VectorModel<crossdesk::ui::RecentConnection>>(
          std::move(connections)));
  window->set_signal_connected(true);
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  window->set_privacy_setting_available(true);
#endif
  if (options.page == "about") {
    window->set_current_version("v1.5.2");
  }
  return true;
}

bool HasNonEmptyEnvironmentVariable(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr && value[0] != '\0';
}

void ResetMainCaptureState(
    const slint::ComponentHandle<crossdesk::ui::MainWindow> &window) {
  window->set_settings_open(false);
  window->set_self_host_settings_open(false);
  window->set_about_open(false);
  window->set_update_open(false);
  window->set_update_available(false);
  window->set_update_check_status(crossdesk::ui::UpdateCheckStatus::Idle);
  window->set_reset_password_open(false);
  window->set_reset_password_invalid(false);
  window->set_new_password_input("");
  window->set_alias_open(false);
  window->set_alias_input("");
  window->set_delete_open(false);
  window->set_offline_warning("");
  window->set_connection_dialog_open(false);
  window->set_connection_status_text("");
  window->set_connection_pending(false);
  window->set_connection_password_required(false);
  window->set_connection_validating(false);
  window->set_permission_dialog_open(false);
  window->set_screen_recording_granted(true);
  window->set_accessibility_granted(true);
  window->set_wayland_notice_open(false);
  window->set_settings_session_active(false);
  window->set_portable_service_settings_visible(false);
  window->set_portable_service_dialog_open(false);
  window->set_portable_service_suppressed_notice_open(false);
}

void ConfigureMainCapturePage(
    const slint::ComponentHandle<crossdesk::ui::MainWindow> &window,
    const CaptureOptions &options) {
  const int language = crossdesk::ui_localization::ApplyMainWindowStrings(
      window, options.language);
  crossdesk::ui_localization::ApplyStreamWindowStrings(window, language);
  window->set_language_index(language);
  ResetMainCaptureState(window);

#if _WIN32
  window->set_custom_titlebar(true);
  window->set_wayland_titlebar(false);
#if CROSSDESK_PORTABLE
  window->set_portable_service_settings_visible(true);
#endif
#elif defined(__linux__)
  const bool has_x11 = HasNonEmptyEnvironmentVariable("DISPLAY");
  const bool use_xwayland =
      has_x11 && HasNonEmptyEnvironmentVariable("WAYLAND_DISPLAY");
  window->set_custom_titlebar(has_x11);
  window->set_wayland_titlebar(use_xwayland);
#else
  window->set_custom_titlebar(false);
  window->set_wayland_titlebar(false);
#endif

  if (options.page == "settings" || options.page == "settings-scroll-hover" ||
      options.page == "settings-scrolled" || options.page == "settings-scroll-drag") {
    window->set_settings_open(true);
  } else if (options.page == "main-menu-badges") {
    window->set_announcement_unread(4);
    window->set_update_available(true);
    window->set_update_open(false);
  } else if (options.page == "settings-deskhub" ||
             options.page == "settings-deskhub-error") {
    window->set_settings_open(true);
    window->set_settings_tab(1);
    if (options.page == "settings-deskhub-error") {
      window->set_gallery_settings_error(
          window->global<crossdesk::ui::GalleryStrings>().invoke_tr(
              "无法保存设置，请重试。", "Unable to save settings. Please try again.",
              "Не удалось сохранить настройки. Повторите попытку."));
    }
  } else if (options.page == "self-hosted") {
    window->set_settings_open(true);
    window->set_self_host_settings_open(true);
  } else if (options.page == "about" || options.page == "about-checking" ||
             options.page == "about-failed" || options.page == "about-update") {
    window->set_current_version("v1.3.5-1-20260924");
    window->set_about_open(true);
    window->set_update_check_status(crossdesk::ui::UpdateCheckStatus::UpToDate);
    if (options.page == "about-checking") {
      window->set_update_check_status(crossdesk::ui::UpdateCheckStatus::Checking);
    } else if (options.page == "about-failed" || options.page == "about-update") {
      window->set_update_available(true);
      window->set_latest_version("v1.3.6-1-20260924");
      window->set_update_check_status(options.page == "about-failed"
          ? crossdesk::ui::UpdateCheckStatus::Failed
          : crossdesk::ui::UpdateCheckStatus::Available);
    }
  } else if (options.page == "update" || options.page == "update-scrolled" ||
             options.page == "update-short") {
    window->set_update_available(true);
    window->set_latest_version("v1.6.1-20261010");
    window->set_release_name("CrossDesk Preview");
    window->set_release_date("2026-10-10");
    // Illustrative notes exercise the production Markdown parser and wrapping,
    // including enough content to inspect both ends of the scrolling viewport.
    const char *notes[] = {
        "**新增功能与改进**\n\n"
        "1. 增强信令快速重连与连接恢复能力；桌面端配置修改无需重新连接信令服务。\n"
        "2. Windows 安装版新增登录前远程控制支持，被控端可随系统启动，在尚未登录时接受远程连接。\n"
        "3. Linux 提供普通版与 Xfce 虚拟桌面版独立安装包，普通版不再强制安装 Xfce 和 Xvfb。\n"
        "4. 被控端会话窗口改为紧凑的屏幕边缘状态栏。\n"
        "5. 完善 iOS / Android 原生控制端，优化远程画面、触控与外接键鼠体验。\n\n"
        "**问题修复**\n\n"
        "6. 修复连接中断后的状态显示，改善高分辨率屏幕上的文字与按钮边界。\n"
        "7. 统一窗口、输入框与按钮的圆角，优化更新提示与长文本滚动体验。",
        "**Features and improvements**\n\n"
        "1. Improve signaling reconnection and session recovery. Desktop configuration changes no longer require reconnecting to the signaling service.\n"
        "2. Add remote control before Windows sign-in, allowing the installed client to accept connections when the system starts.\n"
        "3. Provide separate Linux packages for standard installations and Xfce virtual desktops.\n"
        "4. Replace the controlled session window with a compact status bar at the edge of the screen.\n"
        "5. Improve the native iOS and Android clients, including video, touch, and external keyboard and mouse support.\n\n"
        "**Fixes**\n\n"
        "6. Correct connection status after interruptions and improve text and control borders on high resolution displays.\n"
        "7. Unify corner radii and improve update notifications and scrolling for long release notes.",
        "**Новые возможности и улучшения**\n\n"
        "1. Улучшено восстановление соединения. Изменение настроек больше не требует повторного подключения к серверу сигнализации.\n"
        "2. Добавлено удалённое управление Windows до входа пользователя в систему.\n"
        "3. Подготовлены отдельные пакеты Linux для стандартной установки и виртуальных рабочих столов Xfce.\n"
        "4. Окно управляемого сеанса заменено компактной строкой состояния у края экрана.\n"
        "5. Улучшены клиенты iOS и Android, работа с сенсорным экраном, внешней клавиатурой и мышью.\n\n"
        "**Исправления**\n\n"
        "6. Исправлено отображение состояния после обрыва соединения, улучшена читаемость на экранах высокого разрешения.\n"
        "7. Унифицированы скругления элементов и улучшена прокрутка длинных описаний обновлений."};
    std::vector<crossdesk::ui::ReleaseNoteBlock> blocks;
    for (const auto &parsed : crossdesk::release_notes_text::Parse(notes[language])) {
      crossdesk::ui::ReleaseNoteBlock block{};
      block.content = parsed.content;
      block.section_gap = parsed.section_gap;
      blocks.push_back(std::move(block));
      if (options.page == "update-short" && blocks.size() == 2) break;
    }
    window->set_release_note_blocks(
        std::make_shared<slint::VectorModel<crossdesk::ui::ReleaseNoteBlock>>(blocks));
    window->set_update_open(true);
  } else if (options.page == "reset-password") {
    window->set_reset_password_open(true);
  } else if (options.page == "reset-password-invalid") {
    window->set_reset_password_open(true);
    window->set_reset_password_invalid(true);
    window->set_new_password_input("123");
  } else if (options.page == "alias") {
    window->set_alias_input("Office workstation");
    window->set_alias_open(true);
  } else if (options.page == "delete") {
    window->set_delete_open(true);
  } else if (options.page == "offline") {
    window->set_offline_warning(crossdesk::ui_localization::Text(
        crossdesk::localization::device_offline[language]));
  } else if (options.page == "connection") {
    window->set_connection_status_text(crossdesk::ui_localization::Text(
        crossdesk::localization::p2p_connecting[language]));
    window->set_connection_pending(true);
    window->set_connection_dialog_open(true);
  } else if (options.page == "connection-password") {
    window->set_connection_status_text(crossdesk::ui_localization::Text(
        crossdesk::localization::reinput_password[language]));
    window->set_connection_password_required(true);
    window->set_connection_dialog_open(true);
  } else if (options.page == "service") {
    window->set_portable_service_settings_visible(true);
    window->set_portable_service_dialog_open(true);
  } else if (options.page == "service-notice") {
    window->set_portable_service_settings_visible(true);
    window->set_portable_service_suppressed_notice_open(true);
  } else if (options.page == "permission") {
    window->set_permission_dialog_open(true);
    window->set_screen_recording_granted(false);
    window->set_accessibility_granted(false);
  } else if (options.page == "wayland-notice") {
    window->set_wayland_notice_open(true);
  }
}

slint::Image CreateStreamPreviewImage() {
  constexpr int preview_width = 640;
  constexpr int preview_height = 360;
  slint::SharedPixelBuffer<slint::Rgb8Pixel> pixels(preview_width,
                                                    preview_height);
  auto *data = reinterpret_cast<uint8_t *>(pixels.begin());
  for (int y = 0; y < preview_height; ++y) {
    for (int x = 0; x < preview_width; ++x) {
      const bool border = x < 8 || y < 8 || x >= preview_width - 8 ||
                          y >= preview_height - 8;
      const size_t offset = (static_cast<size_t>(y) * preview_width + x) * 3;
      data[offset] =
          border ? 240 : static_cast<uint8_t>(35 + 150 * x / preview_width);
      data[offset + 1] =
          border ? 70 : static_cast<uint8_t>(35 + 150 * y / preview_height);
      data[offset + 2] = border ? 70 : 90;
    }
  }
  return slint::Image(std::move(pixels));
}

void ConfigureStreamCapturePage(
    const slint::ComponentHandle<crossdesk::ui::StreamWindow> &stream,
    int language) {
  language = crossdesk::ui_localization::ApplyStreamWindowStrings(
      stream, language);
#if defined(__linux__)
  stream->set_custom_titlebar(
      HasNonEmptyEnvironmentVariable("DISPLAY") &&
      HasNonEmptyEnvironmentVariable("WAYLAND_DISPLAY"));
#else
  stream->set_custom_titlebar(false);
#endif
  stream->set_native_video_enabled(false);
  stream->set_window_maximized(false);
  stream->set_fullscreen_enabled(false);
  stream->set_stats_visible(false);
  stream->set_file_transfer_visible(false);
  stream->set_receiving_text("");
  stream->set_status_text("");

  stream->set_session_title("Mac (589173341)");
  stream->set_displays(
      std::make_shared<slint::VectorModel<slint::SharedString>>(
          std::vector{slint::SharedString("Display 1")}));
  stream->set_frame(CreateStreamPreviewImage());
  stream->set_has_frame(true);
  const float height = stream->get_custom_titlebar() ? 752.0f : 720.0f;
  stream->window().set_size(
      slint::LogicalSize(slint::Size<float>{1280.0f, height}));
}

void ConfigureServerCapturePage(
    const slint::ComponentHandle<crossdesk::ui::ServerWindow> &server,
    int language) {
  language = crossdesk::localization::detail::ClampLanguageIndex(language);
  crossdesk::ui::ControllerEntry controller;
  controller.remote_id = "589173341";
  controller.display_name = "Mac";
  server->set_controllers(
      std::make_shared<slint::VectorModel<crossdesk::ui::ControllerEntry>>(
          std::vector{controller}));
  crossdesk::ui_localization::ApplyServerStrings(server, language);
  server->set_file_transfer_visible(false);
  server->set_sending_file(false);
  server->window().set_size(slint::LogicalSize(
      slint::Size<float>{225.0f, server->get_content_height()}));
}

int RunCaptureMode(
    const CaptureOptions &options,
    slint::ComponentHandle<crossdesk::ui::MainWindow> &window,
    slint::ComponentHandle<crossdesk::ui::StreamWindow> &stream,
    slint::ComponentHandle<crossdesk::ui::ServerWindow> &server) {
  window->on_format_remote_id([](slint::SharedString id) { return id; });
  ConfigureMainCapturePage(window, options);
  if (!ConfigureDocumentationDemo(window, options)) {
    return 8;
  }
  // Preview DeskHub with the same demo images, without opening any sessions.
  if (options.page.starts_with("gallery")) {
    window->set_gallery_mode(true);
    std::vector<crossdesk::ui::GalleryTile> tiles;
    const auto recent = window->get_recent_connections();
    for (size_t index = 0; index < recent->row_count(); ++index) {
      const auto device = recent->row_data(index).value();
      crossdesk::ui::GalleryTile tile{};
      tile.id = device.remote_id;
      tile.name = device.display_name;
      tile.online = tile.presence_known = tile.connected = true;
      tile.has_frame = device.has_thumbnail;
      tile.frame = device.thumbnail;
      tile.selected = options.page == "gallery-batch";
      tiles.push_back(tile);
    }
    window->set_gallery_tiles(
        std::make_shared<slint::VectorModel<crossdesk::ui::GalleryTile>>(tiles));
    window->set_gallery_count(static_cast<int>(tiles.size()));
    window->set_gallery_connected(static_cast<int>(tiles.size()));
    window->set_gallery_groups(
        std::make_shared<slint::VectorModel<slint::SharedString>>(
            std::vector{window->global<crossdesk::ui::GalleryStrings>().invoke_tr(
                "全部设备", "All devices", "Все устройства")}));
    window->set_gallery_batch(options.page == "gallery-batch");
    window->set_gallery_selected(options.page == "gallery-batch"
                                    ? static_cast<int>(tiles.size()) : 0);
    window->set_gallery_adding(options.page == "gallery-add");
    if (options.page == "gallery-focus" && !tiles.empty())
      window->set_gallery_focused(tiles.front());
    // A 960 x 720 preview includes the native 22px macOS titlebar.
    const float client_height =
#if defined(__APPLE__)
        698.0f;
#else
        720.0f;
#endif
    window->window().set_size(
        slint::LogicalSize(slint::Size<float>{960.0f, client_height}));
  }
  // Workbench states use the same rendered components and demo-only data.
  if (options.page == "main-empty" || options.page == "main-empty-group") {
    window->set_recent_connections(
        std::make_shared<slint::VectorModel<crossdesk::ui::RecentConnection>>());
    window->set_recent_group_index(options.page == "main-empty-group" ? 1 : 0);
  } else if (options.page == "main-error") {
    window->set_remote_id_input("123");
    window->set_connection_input_error(
        window->global<crossdesk::ui::GalleryStrings>().invoke_tr(
            "请输入完整的 9 位设备 ID", "Enter a complete 9-digit device ID",
            "Введите полный ID устройства из 9 цифр"));
  } else if (options.page == "main-offline") {
    window->set_signal_connected(false);
    window->set_password_visible(false);
    const auto connections = window->get_recent_connections();
    for (size_t index = 0; index < connections->row_count(); ++index) {
      auto device = connections->row_data(index).value();
      device.online = false;
      connections->set_row_data(index, device);
    }
  }
  stream->hide();
  server->hide();

  slint::Window *capture_window = &window->window();
  const bool capture_stream =
      options.page == "stream" || options.page == "stream-video-settings" ||
      options.page == "stream-update" || options.page == "stream-update-legacy";
  if (capture_stream) {
    ConfigureStreamCapturePage(stream, options.language);
    if (!options.demo_assets_path.empty()) {
      // Reuse a public page capture as an explicitly illustrative remote frame.
      const auto frame = slint::Image::load_from_path(slint::SharedString(
          (options.demo_assets_path + "/fixtures/website.png").c_str()));
      if (frame.size().width == 0 || frame.size().height == 0) {
        return 8;
      }
      stream->set_frame(frame);
      stream->set_session_title("Office PC (246813579)");
      stream->set_privacy_can_toggle(true);
      stream->set_privacy_off(true);
      stream->window().set_size(
          slint::LogicalSize(slint::Size<float>{960.0f, 600.0f}));
    }
    if (options.page == "stream-video-settings") {
      stream->set_video_settings_enabled(true);
      stream->set_video_menu_open(true);
    }
    if (options.page == "stream-update" || options.page == "stream-update-legacy") {
      stream->set_remote_update_visible(true);
      stream->on_dismiss_remote_update(
          [stream] { stream->set_remote_update_visible(false); });
    }
    window->hide();
    stream->show();
    capture_window = &stream->window();
  } else if (options.page == "server") {
    ConfigureServerCapturePage(server, options.language);
    window->hide();
    server->show();
    capture_window = &server->window();
  } else {
    window->show();
  }

  if (!options.snapshot_path.empty()) {
    // Let the native backend apply its window size and display scale before
    // taking the snapshot; capturing immediately after show() can yield 1x.
    bool snapshot_written = false;
    slint::Timer snapshot_timer;
    slint::Timer capture_settle_timer;
    slint::Timer pointer_move_timer;
    snapshot_timer.start(slint::TimerMode::SingleShot,
                         std::chrono::milliseconds(250), [&] {
      if (options.page == "main-device-tooltip" ||
          options.page == "main-device-tooltip-moved" ||
          options.page == "main-device-tooltip-edge" ||
          options.page == "main-device-tooltip-exit") {
        const float titlebar = window->get_minimum_window_height() - 472.0f;
        window->window().dispatch_pointer_move_event(
            slint::LogicalPosition(slint::Point<float>{90.0f, titlebar + 330.0f}));
        if (options.page != "main-device-tooltip") {
          // A second event in the same card verifies hover movement without
          // pressing the mouse or relying on another enter/leave event.
          pointer_move_timer.start(slint::TimerMode::SingleShot,
                                   std::chrono::milliseconds(50), [&, titlebar] {
            const bool edge = options.page == "main-device-tooltip-edge";
            const bool leave = options.page == "main-device-tooltip-exit";
            window->window().dispatch_pointer_move_event(slint::LogicalPosition(
                slint::Point<float>{leave ? 2.0f : edge ? window->get_minimum_window_width() - 40.0f : 170.0f,
                    leave ? 2.0f : titlebar + 340.0f}));
          });
        }
      } else if (options.page == "settings-scroll-hover" ||
                 options.page == "settings-scrolled" ||
                 options.page == "settings-scroll-drag") {
        const float dialog_x = (window->get_minimum_window_width() - 400.0f) / 2;
        const float dialog_y = (window->get_minimum_window_height() - 446.0f) / 2;
        const auto scrollbar_position = slint::LogicalPosition(
            slint::Point<float>{dialog_x + 389.0f, dialog_y + 150.0f});
        if (options.page == "settings-scrolled") {
          window->window().dispatch_pointer_scroll_event(
              slint::LogicalPosition(slint::Point<float>{320.0f, 260.0f}),
              0.0f, -2000.0f);
        }
        window->window().dispatch_pointer_move_event(scrollbar_position);
        if (options.page == "settings-scroll-drag") {
          window->window().dispatch_pointer_press_event(
              scrollbar_position, slint::PointerEventButton::Left);
          const auto drag_end = slint::LogicalPosition(
              slint::Point<float>{dialog_x + 389.0f, dialog_y + 340.0f});
          window->window().dispatch_pointer_move_event(drag_end);
          window->window().dispatch_pointer_release_event(
              drag_end, slint::PointerEventButton::Left);
        }
      } else if (options.page == "update-scrolled") {
        window->window().dispatch_pointer_scroll_event(
            slint::LogicalPosition(slint::Point<float>{320.0f, 260.0f}),
            0.0f, -2000.0f);
        window->window().dispatch_pointer_move_event(
            slint::LogicalPosition(slint::Point<float>{2.0f, 2.0f}));
      } else if (options.page == "main-recent-scrolled") {
        window->window().dispatch_pointer_scroll_event(
            slint::LogicalPosition(slint::Point<float>{
                400.0f, window->get_minimum_window_height() - 472.0f + 360.0f}),
            -400.0f, 0.0f);
        window->window().dispatch_pointer_move_event(
            slint::LogicalPosition(slint::Point<float>{2.0f, 2.0f}));
      } else if (options.page == "main-menu" ||
                 options.page == "main-menu-badges") {
        const auto menu_position = slint::LogicalPosition(
            slint::Point<float>{window->get_minimum_window_width() - 38.0f,
                window->get_minimum_window_height() - 472.0f + 20.0f});
        window->window().dispatch_pointer_press_event(
            menu_position, slint::PointerEventButton::Left);
        window->window().dispatch_pointer_release_event(
            menu_position, slint::PointerEventButton::Left);
        window->window().dispatch_pointer_move_event(
            slint::LogicalPosition(slint::Point<float>{2.0f, 2.0f}));
      } else if (options.page == "main-copy-tooltip" ||
                 options.page == "main-password-tooltip") {
        const bool copy = options.page == "main-copy-tooltip";
        window->window().dispatch_pointer_move_event(slint::LogicalPosition(
            slint::Point<float>{window->get_minimum_window_width() / 2 - (copy ? 48.0f : 42.0f),
                window->get_minimum_window_height() - 472.0f +
                    (copy ? 150.0f : 216.0f)}));
      } else if (options.page == "main-copy-released" ||
                 options.page == "main-copy-released-outside" ||
                 options.page == "main-keyboard-tab") {
        const auto copy_position = slint::LogicalPosition(slint::Point<float>{
            window->get_minimum_window_width() / 2 - 48.0f,
            window->get_minimum_window_height() - 472.0f + 150.0f});
        window->window().dispatch_pointer_move_event(copy_position);
        window->window().dispatch_pointer_press_event(
            copy_position, slint::PointerEventButton::Left);
        window->window().dispatch_pointer_release_event(
            copy_position, slint::PointerEventButton::Left);
        if (options.page != "main-copy-released") {
          window->window().dispatch_pointer_move_event(
              slint::LogicalPosition(slint::Point<float>{2.0f, 2.0f}));
        }
        if (options.page == "main-keyboard-tab") {
          // Traverse the remote ID input and focus the Connect button.
          for (int index = 0; index < 2; ++index) {
            window->window().dispatch_key_press_event("\t");
            window->window().dispatch_key_release_event("\t");
          }
        }
      }
      // Let settings scroll animations settle as well as hover/focus feedback.
      capture_settle_timer.start(slint::TimerMode::SingleShot,
                                std::chrono::milliseconds(
                                    options.page.rfind("settings", 0) == 0 ? 350 : 150), [&] {
        snapshot_written =
            WriteWindowSnapshot(*capture_window, options.snapshot_path.c_str());
        slint::quit_event_loop();
      });
    });
    slint::run_event_loop();
    return snapshot_written ? 0 : 7;
  }

  if (capture_stream || options.page == "server") {
    slint::run_event_loop();
  } else {
    window->run();
  }
  return 0;
}

// Sample the native window while the same restoration used by the application
// runs. A component-only screenshot used to miss the conflicting restore width.
bool VerifyMainWindowGeometry() {
  for (bool restore : {false, true}) {
    auto component = crossdesk::ui::MainWindow::create();
    auto &window = component->window();
    crossdesk::gallery::WindowState placement;
    const float width = component->get_minimum_window_width();
    const float height = component->get_minimum_window_height();
    if (width != 640.0f || height != 472.0f) return false;
    if (restore) {
      placement.BeginRestore();
      placement.PrepareForShow(
          window, false,
          placement.RestoreSize(false, window.scale_factor(), height, width),
          std::nullopt);
    }
    window.show();
    bool stable = true;
    int samples = 0;
    slint::Timer timer;
    timer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(16), [&] {
      const auto actual = window.size();
      const auto expected = placement.RestoreSize(
          false, window.scale_factor(), height, width);
      if (actual.width != expected.width || actual.height != expected.height) {
        std::cerr << "Startup geometry changed: restore=" << restore
                  << " sample=" << samples << " actual=" << actual.width
                  << 'x' << actual.height << " expected=" << expected.width
                  << 'x' << expected.height << '\n';
        stable = false;
      }
      if (placement.RestoringNormal()) {
        if (!placement.NormalSizeRestored(actual, expected))
          window.set_size(expected);
      } else if (placement.Restoring()) {
        // Match the application's fixed connection-window role. AppKit can
        // report a fixed-size window as zoomed even at its normal dimensions.
        placement.FinishRestore(false, actual, false, false);
      }
      if (++samples == 20) slint::quit_event_loop();
    });
    slint::run_event_loop();
    timer.stop();
    window.hide();
    if (!stable || placement.Restoring()) return false;
  }
  return true;
}

// Exercise hit targets after the workbench layout change, rather than calling
// callbacks directly: overlapping surfaces must not swallow these actions.
bool VerifyWorkbenchActions() {
  auto window = crossdesk::ui::MainWindow::create();
  const float width = window->get_minimum_window_width();
  window->window().set_size(
      slint::LogicalSize(slint::Size<float>{width, window->get_minimum_window_height()}));
  auto click = [&](float x, float y) {
    const auto position = slint::LogicalPosition(slint::Point<float>{x, y});
    window->window().dispatch_pointer_press_event(
        position, slint::PointerEventButton::Left);
    window->window().dispatch_pointer_release_event(
        position, slint::PointerEventButton::Left);
  };
  int copies = 0, visibility_changes = 0, refreshes = 0, connections = 0;
  bool gallery_opened = false;
  window->on_copy_local_id([&] { ++copies; return true; });
  window->on_toggle_password_visibility([&] { ++visibility_changes; });
  window->on_refresh_password([&] { ++refreshes; });
  window->on_open_gallery([&] { gallery_opened = true; });
  window->set_remote_id_input("987 654 321");
  window->on_connect_requested([&](slint::SharedString id) {
    if (id == "987 654 321") ++connections;
  });
  click(width / 2 - 48, 150);
  click(width / 2 - 118, 216);
  click(width / 2 - 80, 216);
  click(width / 2 - 42, 216);
  if (copies != 1 || visibility_changes != 1 || refreshes != 1 ||
      !window->get_reset_password_open()) return false;
  window->set_reset_password_open(false);
  window->set_password_change_pending(true);
  click(width / 2 - 80, 216);
  click(width / 2 - 42, 216);
  if (refreshes != 1 || window->get_reset_password_open()) return false;
  click(width * 0.75f - 6, 216);
  // Space activates the focused primary button; pending state blocks repeats.
  window->window().dispatch_key_press_event(" ");
  window->window().dispatch_key_release_event(" ");
  if (connections != 2) return false;
  window->set_connection_pending(true);
  click(width * 0.75f - 6, 216);
  if (connections != 2) return false;
  window->set_connection_pending(false);
  click(width - 38, 20);
  click(width - 78, 56);
  if (!window->get_settings_open()) return false;
  // Both tabs must remain at the same position, including with a save error.
  click(width / 2 + 87, 89);
  if (window->get_settings_tab() != 1) return false;
  click(width / 2 - 87, 89);
  if (window->get_settings_tab() != 0) return false;
  click(width / 2 + 87, 89);
  window->set_gallery_settings_error("Unable to save settings. Please try again.");
  click(width / 2 - 87, 89);
  if (window->get_settings_tab() != 0) return false;
  window->set_gallery_settings_error("");
  window->set_settings_open(false);
  // DeskHub follows the ID/IP mode switch when direct IP connections are enabled.
  click(window->get_ip_direct_enabled() ? 204 : 60, 20);
  if (!gallery_opened) return false;
  crossdesk::ui::RecentConnection device{};
  device.remote_id = "987654321";
  device.display_name = "Workstation";
  device.online = true;
  window->set_recent_connections(
      std::make_shared<slint::VectorModel<crossdesk::ui::RecentConnection>>(
          std::vector{device}));
  bool recent_connected = false;
  window->on_recent_connect([&](slint::SharedString id) {
    recent_connected = id == "987654321";
  });
  click(100, 344);
  if (!recent_connected) return false;
  bool password_submitted = false, cancelled = false;
  window->on_connection_submit_password([&](slint::SharedString value, bool) {
    password_submitted = value == "654321";
  });
  window->on_connection_cancel([&] { cancelled = true; });
  window->set_connection_password("654321");
  window->set_connection_password_required(true);
  window->set_connection_dialog_open(true);
  click(width / 2 + 62, 312);
  click(width / 2 - 62, 312);
  if (!password_submitted || !cancelled) return false;
  window->set_connection_dialog_open(false);

  // Rounded frames must preserve editor focus, text bindings and Enter actions.
  auto type = [&](std::string_view text) {
    for (char character : text) {
      const slint::SharedString key(std::string(1, character).c_str());
      window->window().dispatch_key_press_event(key);
      window->window().dispatch_key_release_event(key);
    }
  };
  bool renamed = false;
  window->on_recent_edit_alias([&](slint::SharedString, slint::SharedString name) {
    renamed = name == "Office";
  });
  window->set_alias_input("");
  window->set_alias_open(true);
  click(width / 2, 238);
  type("Office\n");
  if (!renamed || window->get_alias_open()) {
    std::cerr << "Alias editor did not accept the typed name; text="
              << std::string(window->get_alias_input()) << '\n';
    return false;
  }

  bool added = false;
  window->on_gallery_add_device([&](slint::SharedString id,
      slint::SharedString password, slint::SharedString group, bool) {
    added = id == "123456789" && password == "123456" && group == "Lab";
    return added;
  });
  window->set_gallery_mode(true);
  window->window().set_size(slint::LogicalSize(slint::Size<float>{width, 448.0f}));
  window->set_gallery_adding(true);
  click(width / 2, 142);
  type("123456789\n123456\nLab\n");
  if (!added || window->get_gallery_adding()) {
    std::cerr << "Add-device editors did not retain Enter focus traversal\n";
    return false;
  }
  return true;
}

bool VerifySystemShortcutMenu() {
  auto stream = crossdesk::ui::StreamWindow::create();
  stream->window().set_size(slint::LogicalSize(slint::Size<float>{1280, 720}));
  stream->show();
  std::vector<std::string> commands;
  stream->on_send_shortcut([&](slint::SharedString command) { commands.emplace_back(command); });
  const auto settle = [] {
    for (int i = 0; i < 20; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      slint::platform::update_timers_and_animations();
    }
  };
  const auto click = [&](float x, float y) {
    const slint::LogicalPosition point({x, stream->get_video_area_top() + y});
    stream->window().dispatch_pointer_move_event(point);
    stream->window().dispatch_pointer_press_event(point, slint::PointerEventButton::Left);
    stream->window().dispatch_pointer_release_event(point, slint::PointerEventButton::Left);
  };
  // The toolbar starts collapsed. Reveal it before targeting its buttons.
  click(19, 19);
  settle();
  stream->set_secure_attention_supported(true);
  settle();
  click(90, 19);
  if (!stream->invoke_is_local_control_area(90, 90)) return false;
  click(90, 68);

  // Switching away from a Windows host hides the entire shortcut entry and
  // closes an open menu; its former rows must no longer dispatch commands.
  click(90, 19);
  if (!stream->invoke_is_local_control_area(90, 90)) return false;
  stream->set_secure_attention_supported(false);
  settle();
  if (stream->invoke_is_local_control_area(90, 90)) return false;
  click(90, 98);
  if (commands != std::vector<std::string>{"Ctrl+Alt+Del"}) return false;

  stream->set_secure_attention_supported(true);
  settle();
  click(90, 19);
  click(90, 98);
  return commands == std::vector<std::string>{"Ctrl+Alt+Del", "Win+L"};
}

bool VerifyRemoteIdEditing() {
  auto window = crossdesk::ui::MainWindow::create();
  window->window().set_size(slint::LogicalSize(slint::Size<float>{
      window->get_minimum_window_width(), window->get_minimum_window_height()}));
  window->on_format_remote_id([](slint::SharedString text) { return text; });
  window->invoke_reset_remote_id();
  const auto key = [&](const slint::SharedString& text) {
    window->window().dispatch_key_press_event(text);
    window->window().dispatch_key_release_event(text);
  };
  const auto settle = [] {
    for (int i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      slint::platform::update_timers_and_animations();
    }
  };
  settle(); // Let the initial timer state settle before the first user edit.
  for (char digit : std::string("1234")) {
    key(slint::SharedString(std::string(1, digit)));
    settle(); // Separate edits must not leave the formatter repeating.
  }
  if (std::string(window->get_remote_id_input()) != "1234") return false;
  window->window().dispatch_key_press_event(slint::platform::key_codes::Control);
  key("a");
  window->window().dispatch_key_release_event(slint::platform::key_codes::Control);
  settle(); // Formatting must not repeatedly collapse the selection.
  key("5");
  settle();
  return std::string(window->get_remote_id_input()) == "5";
}

} // namespace

int main() {
  if (!VerifySystemShortcutMenu()) {
    std::cerr << "Shortcut menu must dispatch Windows shortcuts and hide for unsupported hosts\n";
    return 13;
  }
  if (!VerifyRemoteIdEditing()) {
    std::cerr << "Remote ID editor must preserve Ctrl+A selection after formatting\n";
    return 12;
  }
  const CaptureOptions capture_options = LoadCaptureOptions();
  if (!VerifyMainWindowGeometry()) {
    std::cerr << "Main window dimensions must stay stable through startup\n";
    return 11;
  }
  if (!VerifyWorkbenchActions()) {
    std::cerr << "Workbench controls must retain their pointer and keyboard actions\n";
    return 10;
  }
  auto window = crossdesk::ui::MainWindow::create();
  auto stream = crossdesk::ui::StreamWindow::create();
  auto server = crossdesk::ui::ServerWindow::create();
  if (!RegisterFontAwesome(window->window()) ||
      !RegisterFontAwesome(stream->window()) ||
      !RegisterFontAwesome(server->window())) {
    return 1;
  }
  window->set_local_id("123 456 789");
  window->set_local_password("123456");
  window->set_connection_dialog_open(true);
  window->set_connection_password_required(true);
  window->set_custom_titlebar(true);
  window->set_wayland_titlebar(true);
  window->set_settings_session_active(true);
  window->set_portable_service_settings_visible(true);
  assert(std::string(window->get_local_id()) == "123 456 789");
  assert(window->get_connection_dialog_open());
  assert(window->get_connection_password_required());
  assert(window->get_custom_titlebar());
  assert(window->get_wayland_titlebar());
  assert(window->get_settings_session_active());
  assert(window->get_portable_service_settings_visible());
  if (const char *snapshot_path =
          std::getenv("CROSSDESK_MAIN_UI_SNAPSHOT")) {
    window->show();
    const bool snapshot_written =
        WriteWindowSnapshot(window->window(), snapshot_path);
    window->hide();
    if (!snapshot_written) {
      return 2;
    }
  }
  if (const char *snapshot_path =
          std::getenv("CROSSDESK_MAIN_INACTIVE_UI_SNAPSHOT")) {
    window->set_window_active(false);
    window->show();
    const bool snapshot_written =
        WriteWindowSnapshot(window->window(), snapshot_path);
    window->hide();
    window->set_window_active(true);
    if (!snapshot_written) {
      return 3;
    }
  }
  window->set_settings_open(true);
  window->set_about_open(true);
  assert(window->get_settings_open());
  assert(window->get_about_open());
  if (window->get_update_open()) {
    std::cerr << "The update dialog must stay closed before an update is found\n";
    return 8;
  }
  window->set_update_available(true);
  if (!window->get_update_open()) {
    std::cerr << "Finding an update must automatically open the update dialog\n";
    return 8;
  }
  // The Later button closes the dialog by assigning update-open = false.
  window->set_update_open(false);
  window->set_update_available(true);
  if (window->get_update_open() || !window->get_update_available()) {
    std::cerr << "Repeated checks must preserve dismissal and the update badge\n";
    return 8;
  }
  bool update_check_requested = false;
  window->on_check_for_updates([&] {
    update_check_requested = true;
    window->set_update_check_status(crossdesk::ui::UpdateCheckStatus::Checking);
  });
  window->invoke_check_for_updates();
  if (!update_check_requested || window->get_update_check_status() !=
                                     crossdesk::ui::UpdateCheckStatus::Checking) {
    std::cerr << "Manual update check must expose the checking state\n";
    return 8;
  }
  window->set_update_available(false);
  window->set_update_available(true);
  if (window->get_update_open()) {
    std::cerr << "A dismissed update dialog must stay closed during this launch\n";
    return 8;
  }
  window->set_update_open(true);
  if (!window->get_update_open()) {
    std::cerr << "Update details must still be available after dismissal\n";
    return 8;
  }
  window->set_update_open(false);
  {
    auto next_launch = crossdesk::ui::MainWindow::create();
    next_launch->set_update_available(true);
    if (!next_launch->get_update_open()) {
      std::cerr << "A new launch must show the available update again\n";
      return 8;
    }
  }
  window->set_update_available(false);
  window->set_update_check_status(crossdesk::ui::UpdateCheckStatus::Idle);
  if (const char *snapshot_path =
          std::getenv("CROSSDESK_SETTINGS_UI_SNAPSHOT")) {
    window->set_about_open(false);
    window->set_connection_dialog_open(false);
    window->show();
    const bool snapshot_written =
        WriteWindowSnapshot(window->window(), snapshot_path);
    window->hide();
    if (!snapshot_written) {
      return 6;
    }
    window->set_connection_dialog_open(true);
    window->set_about_open(true);
  }
  window->set_remote_id_input("987654321");
  window->invoke_reset_remote_id();
  assert(std::string(window->get_remote_id_input()).empty());

  std::vector<crossdesk::ui::RecentConnection> connections;
  crossdesk::ui::RecentConnection connection{};
  connection.remote_id = "987654321";
  connection.display_name = "Office PC";
  connection.host_name = "office-pc";
  connection.online = true;
  connections.emplace_back(std::move(connection));
  window->set_recent_connections(
      std::make_shared<slint::VectorModel<crossdesk::ui::RecentConnection>>(
          std::move(connections)));
  assert(window->get_recent_connections()->row_count() == 1);

  bool connect_requested = false;
  window->on_connect_requested([&](slint::SharedString remote_id) {
    connect_requested = std::string(remote_id) == "987654321";
  });
  window->invoke_connect_requested("987654321");
  assert(connect_requested);

  bool password_submitted = false;
  window->on_connection_submit_password(
      [&](slint::SharedString password, bool remember) {
        password_submitted = std::string(password) == "654321" && remember;
      });
  window->invoke_connection_submit_password("654321", true);
  assert(password_submitted);

  stream->set_session_title("Remote host (123456789)");
  stream->set_custom_titlebar(true);
  stream->set_native_video_enabled(true);
  stream->set_window_maximized(true);
  assert(stream->get_session_title() == "Remote host (123456789)");
  assert(stream->get_custom_titlebar());
  assert(stream->get_native_video_enabled());
  assert(stream->get_window_maximized());
  // Keep snapshots on the CPU-rendered UI path; the native Metal video view
  // is intentionally outside Slint's component snapshot.
  stream->set_native_video_enabled(false);
  if (const char *snapshot_path =
          std::getenv("CROSSDESK_STREAM_UI_SNAPSHOT")) {
    stream->set_window_maximized(false);
    stream->show();
    const bool snapshot_written =
        WriteWindowSnapshot(stream->window(), snapshot_path);
    stream->hide();
    stream->set_window_maximized(true);
    if (!snapshot_written) {
      return 4;
    }
  }
  if (const char *snapshot_path =
          std::getenv("CROSSDESK_STREAM_INACTIVE_UI_SNAPSHOT")) {
    stream->set_window_maximized(false);
    stream->set_window_active(false);
    stream->show();
    const bool snapshot_written =
        WriteWindowSnapshot(stream->window(), snapshot_path);
    stream->hide();
    stream->set_window_active(true);
    stream->set_window_maximized(true);
    if (!snapshot_written) {
      return 5;
    }
  }

  bool maximize_requested = false;
  stream->on_toggle_maximize_stream_window(
      [&] { maximize_requested = true; });
  stream->invoke_toggle_maximize_stream_window();
  assert(maximize_requested);

  bool keyboard_focus_changed = false;
  bool keyboard_input_received = false;
  stream->on_keyboard_focus_changed(
      [&](bool focused) { keyboard_focus_changed = focused; });
  stream->on_key_input(
      [&](slint::SharedString text, bool pressed, bool, bool, bool, bool) {
        keyboard_input_received = std::string(text) == "a" && pressed;
      });
  stream->window().set_size(
      slint::LogicalSize(slint::Size<float>{1280.0f, 720.0f}));
  stream->window().dispatch_pointer_press_event(
      slint::LogicalPosition(slint::Point<float>{640.0f, 360.0f}),
      slint::PointerEventButton::Left);
  stream->window().dispatch_pointer_release_event(
      slint::LogicalPosition(slint::Point<float>{640.0f, 360.0f}),
      slint::PointerEventButton::Left);
  stream->window().dispatch_key_press_event("a");
  assert(keyboard_focus_changed);
  assert(keyboard_input_received);

  stream->window().dispatch_key_release_event("a");

  // The modal owns keyboard and pointer input. Complete a gesture that was
  // already sent to the peer before showing it, then block new gestures.
  int remote_key_events = 0;
  int remote_pointer_presses = 0;
  int remote_pointer_releases = 0;
  int notice_dismissals = 0;
  stream->on_key_input(
      [&](slint::SharedString, bool, bool, bool, bool, bool) {
        ++remote_key_events;
      });
  stream->on_pointer_input(
      [&](slint::PointerEventButton, slint::PointerEventKind kind, float, float) {
        if (kind == slint::PointerEventKind::Down) ++remote_pointer_presses;
        if (kind == slint::PointerEventKind::Up) ++remote_pointer_releases;
      });
  stream->on_dismiss_remote_update([&] {
    ++notice_dismissals;
    stream->set_remote_update_visible(false);
  });
  const auto outside_notice =
      slint::LogicalPosition(slint::Point<float>{640.0f, 500.0f});
  stream->window().dispatch_pointer_press_event(
      outside_notice, slint::PointerEventButton::Left);
  stream->set_remote_update_visible(true);
  stream->window().dispatch_pointer_release_event(
      outside_notice, slint::PointerEventButton::Left);
  if (remote_pointer_presses != 1 || remote_pointer_releases != 1) {
    std::cerr << "Showing the upgrade dialog must finish an active remote gesture\n";
    return 9;
  }
  const float notice_y =
      std::max(64.0f, stream->get_video_area_height() * 0.18f) + 24.0f;
  if (!stream->invoke_is_local_control_area(640.0f, notice_y) ||
      !stream->invoke_is_local_control_area(640.0f, 500.0f)) {
    std::cerr << "Remote upgrade dialog and backdrop must consume local input\n";
    return 9;
  }
  stream->window().dispatch_pointer_press_event(
      outside_notice, slint::PointerEventButton::Left);
  stream->window().dispatch_pointer_release_event(
      outside_notice, slint::PointerEventButton::Left);
  stream->window().dispatch_key_press_event("a");
  stream->window().dispatch_key_release_event("a");
  if (remote_pointer_presses != 1 || remote_pointer_releases != 1 ||
      remote_key_events != 0 || notice_dismissals != 0) {
    std::cerr << "Upgrade dialog input must stay local\n";
    return 9;
  }
  stream->window().dispatch_key_press_event("\x1b");
  stream->window().dispatch_key_release_event("\x1b");
  if (stream->get_remote_update_visible() || notice_dismissals != 1 ||
      remote_key_events != 0) {
    std::cerr << "Escape must dismiss the dialog without forwarding the key\n";
    return 9;
  }
  if (stream->invoke_is_local_control_area(640.0f, notice_y)) {
    std::cerr << "Dismissed upgrade notice must release its input area\n";
    return 9;
  }
  stream->window().dispatch_key_press_event("a");
  stream->window().dispatch_key_release_event("a");
  stream->window().dispatch_pointer_press_event(
      outside_notice, slint::PointerEventButton::Left);
  stream->window().dispatch_pointer_release_event(
      outside_notice, slint::PointerEventButton::Left);
  if (remote_key_events != 2 || remote_pointer_presses != 2 ||
      remote_pointer_releases != 2) {
    std::cerr << "Dismissing the upgrade dialog must restore remote input\n";
    return 9;
  }

  stream->set_remote_update_visible(true);
  const auto notice_button = slint::LogicalPosition(slint::Point<float>{
      764.0f, stream->get_video_area_top() + notice_y - 24.0f + 124.0f});
  stream->window().dispatch_pointer_press_event(
      notice_button, slint::PointerEventButton::Left);
  stream->window().dispatch_pointer_release_event(
      notice_button, slint::PointerEventButton::Left);
  if (stream->get_remote_update_visible() || notice_dismissals != 2 ||
      remote_pointer_presses != 2 || remote_pointer_releases != 2) {
    std::cerr << "Upgrade confirmation click must not reach the peer\n";
    return 9;
  }

  crossdesk::ui::FileTransferEntry transfer;
  transfer.name = "archive.zip";
  transfer.status = "Sending";
  transfer.progress = 0.5f;
  transfer.speed = "1.0 mbps";
  transfer.size = "2.00 MB";
  stream->set_file_transfers(
      std::make_shared<
          slint::VectorModel<crossdesk::ui::FileTransferEntry>>(
          std::vector{transfer}));
  stream->set_file_transfer_visible(true);
  crossdesk::ui::NetworkStatsRow video_stats;
  video_stats.label = "Video";
  video_stats.inbound = "427 kbps";
  video_stats.outbound = "5 kbps";
  video_stats.loss_rate = "0%";
  stream->set_stats_rows(
      std::make_shared<
          slint::VectorModel<crossdesk::ui::NetworkStatsRow>>(
          std::vector{video_stats}));
  stream->set_stats_fps("53");
  stream->set_stats_resolution("3024x1964");
  stream->set_stats_connection_mode("Direct");
  stream->set_stats_visible(true);
  stream->set_fullscreen_enabled(true);
  assert(stream->get_file_transfers()->row_count() == 1);
  assert(stream->get_file_transfer_visible());
  assert(stream->get_stats_rows()->row_count() == 1);
  assert(std::string(stream->get_stats_fps()) == "53");
  assert(std::string(stream->get_stats_resolution()) == "3024x1964");
  assert(std::string(stream->get_stats_connection_mode()) == "Direct");
  assert(stream->get_stats_visible());
  assert(stream->get_fullscreen_enabled());

  bool display_switched = false;
  stream->on_switch_display(
      [&](int index) { display_switched = index == 1; });
  stream->invoke_switch_display(1);
  assert(display_switched);

  auto &stream_strings = stream->global<crossdesk::ui::StreamStrings>();
  stream_strings.set_select_display("Select Display");
  stream_strings.set_expand_control("Expand Control Bar");
  assert(std::string(stream_strings.get_select_display()) == "Select Display");
  assert(std::string(stream_strings.get_expand_control()) ==
         "Expand Control Bar");

  crossdesk::ui::ControllerEntry controller;
  controller.remote_id = "123456789";
  controller.display_name = "Remote host";
  server->set_controllers(
      std::make_shared<slint::VectorModel<crossdesk::ui::ControllerEntry>>(
          std::vector{controller}));
  server->set_file_transfer_visible(true);
  server->set_sending_file(true);
  server->set_file_progress(0.5f);
  server->set_current_file_name("archive.zip");
  server->set_file_size_text("1.00 MB / 2.00 MB");
  assert(server->get_controllers()->row_count() == 1);
  assert(server->get_file_transfer_visible());
  assert(server->get_sending_file());
  assert(server->get_file_progress() == 0.5f);

  bool controller_selected = false;
  server->on_controller_selected(
      [&](int index) { controller_selected = index == 0; });
  server->invoke_controller_selected(0);
  assert(controller_selected);

  if (capture_options.enabled) {
    return RunCaptureMode(capture_options, window, stream, server);
  }

  return 0;
}
