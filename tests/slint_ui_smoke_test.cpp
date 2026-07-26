#include "crossdesk_ui.h"
#include "fa_solid_900.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
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

} // namespace

int main() {
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
  stream->set_window_maximized(true);
  assert(stream->get_tabs()->row_count() == 1);
  assert(stream->get_custom_titlebar());
  assert(stream->get_window_maximized());
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

  return 0;
}
