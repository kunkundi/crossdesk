#include "crossdesk_ui.h"
#include "fa_solid_900.h"
#include "ui/ui_localization.h"

#include <cassert>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
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
  return options;
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

  if (options.page == "settings") {
    window->set_settings_open(true);
  } else if (options.page == "self-hosted") {
    window->set_settings_open(true);
    window->set_self_host_settings_open(true);
  } else if (options.page == "about") {
    window->set_current_version("0.0.0");
    window->set_about_open(true);
  } else if (options.page == "update") {
    window->set_update_available(true);
    window->set_latest_version("v0.0.1");
    window->set_release_name("CrossDesk Preview");
    window->set_release_date("2026-08-25");
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

  crossdesk::ui::StreamTab tab;
  tab.remote_id = "589173341";
  tab.title = "Mac";
  tab.connected = true;
  stream->set_tabs(
      std::make_shared<slint::VectorModel<crossdesk::ui::StreamTab>>(
          std::vector{tab}));
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
  server->set_controller_names(
      std::make_shared<slint::VectorModel<slint::SharedString>>(
          std::vector{slint::SharedString("Mac")}));
  server->set_language_index(language);
  server->set_controller_label(crossdesk::ui_localization::Text(
      crossdesk::localization::controller[language]));
  server->set_connection_label(crossdesk::ui_localization::Text(
      crossdesk::localization::connection_status[language]));
  server->set_connection_status(crossdesk::ui_localization::Text(
      crossdesk::localization::p2p_connected[language]));
  server->set_file_transfer_label(crossdesk::ui_localization::Text(
      crossdesk::localization::file_transfer[language]));
  server->set_select_file_label(crossdesk::ui_localization::Text(
      crossdesk::localization::select_file[language]));
  server->set_file_transfer_visible(false);
  server->set_sending_file(false);
  const float width = language == 0 ? 250.0f : language == 1 ? 330.0f : 430.0f;
  server->window().set_size(
      slint::LogicalSize(slint::Size<float>{width, 150.0f}));
}

int RunCaptureMode(
    const CaptureOptions &options,
    slint::ComponentHandle<crossdesk::ui::MainWindow> &window,
    slint::ComponentHandle<crossdesk::ui::StreamWindow> &stream,
    slint::ComponentHandle<crossdesk::ui::ServerWindow> &server) {
  ConfigureMainCapturePage(window, options);
  stream->hide();
  server->hide();

  slint::Window *capture_window = &window->window();
  if (options.page == "stream") {
    ConfigureStreamCapturePage(stream, options.language);
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
    return WriteWindowSnapshot(*capture_window, options.snapshot_path.c_str())
               ? 0
               : 7;
  }

  if (options.page == "stream" || options.page == "server") {
    slint::run_event_loop();
  } else {
    window->run();
  }
  return 0;
}

} // namespace

int main() {
  const CaptureOptions capture_options = LoadCaptureOptions();
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
  window->set_hardware_codec_available(false);
  window->set_portable_service_settings_visible(true);
  assert(std::string(window->get_local_id()) == "123 456 789");
  assert(window->get_connection_dialog_open());
  assert(window->get_connection_password_required());
  assert(window->get_custom_titlebar());
  assert(window->get_wayland_titlebar());
  assert(window->get_settings_session_active());
  assert(!window->get_hardware_codec_available());
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
  crossdesk::ui::RecentConnection connection;
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

  crossdesk::ui::StreamTab tab;
  tab.remote_id = "123456789";
  tab.title = "Remote host";
  tab.connected = true;
  stream->set_tabs(
      std::make_shared<slint::VectorModel<crossdesk::ui::StreamTab>>(
          std::vector{tab}));
  stream->set_custom_titlebar(true);
  stream->set_native_video_enabled(true);
  stream->set_window_maximized(true);
  assert(stream->get_tabs()->row_count() == 1);
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

  bool tab_reordered = false;
  stream->on_reorder_tab([&](int index, float x, float width) {
    tab_reordered = index == 0 && x == 120.0f && width == 110.0f;
  });
  stream->invoke_reorder_tab(0, 120.0f, 110.0f);
  assert(tab_reordered);

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
