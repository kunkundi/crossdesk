#include "crossdesk_ui.h"
#include "slint_render_test_platform.h"
#include "ui/model_update.h"
#include "ui/ui_localization.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>

using namespace std::chrono_literals;

namespace {
bool Check(bool value, const char* message) {
  if (!value) std::cerr << message << '\n';
  return value;
}

auto CreateMainWindow() {
  auto main = crossdesk::ui::MainWindow::create();
  // Text and language options now come from the application. Load them before
  // measuring or clicking layouts whose geometry depends on their labels.
  crossdesk::ui_localization::ApplyMainWindowStrings(main, 0);
  main->set_language_index(0);
  return main;
}

bool VerifyStreamPopups(RenderTestPlatform& platform) {
  auto stream = crossdesk::ui::StreamWindow::create();
  auto& screen = *platform.adapter;
  stream->window().set_size(slint::LogicalSize({1280, 720}));
  stream->set_video_settings_enabled(true);
  // Exercise the Windows toolbar, including shortcuts and privacy controls.
  stream->set_secure_attention_supported(true);
  stream->set_privacy_visible(true);
  stream->show();
  const int top = int(stream->get_video_area_top());
  auto displays = std::make_shared<slint::VectorModel<slint::SharedString>>();
  auto transfers = std::make_shared<slint::VectorModel<crossdesk::ui::FileTransferEntry>>();
  stream->set_displays(displays);
  stream->set_file_transfers(transfers);
  bool file_visible = false;
  unsigned ticks = 0;
  slint::Timer sync;
  sync.start(slint::TimerMode::Repeated, 16ms, [&] {
    // Match the live session's model synchronization, including long clicks.
    crossdesk::ui::UpdateModelRows(*displays,
        std::vector<slint::SharedString>{"Display 1", "Display 2", "Display 3"});
    crossdesk::ui::FileTransferEntry entry{};
    entry.name = "test.txt"; entry.progress = (++ticks % 100) / 100.f;
    crossdesk::ui::UpdateModelRows(*transfers, std::vector(8, entry));
    stream->set_file_transfer_visible(file_visible);
  });
  int remote_presses = 0, remote_releases = 0, remote_scrolls = 0;
  stream->on_pointer_input([&](auto, auto kind, float, float) {
    if (kind == slint::PointerEventKind::Down) ++remote_presses;
    if (kind == slint::PointerEventKind::Up) ++remote_releases;
  });
  stream->on_scroll_input([&](float, float, float, float) { ++remote_scrolls; });
  int selected_display = -1;
  stream->on_switch_display([&](int index) { selected_display = index; });
  std::string shortcut;
  stream->on_send_shortcut([&](slint::SharedString text) { shortcut = text; });
  int video_changes = 0;
  stream->on_change_video_settings([&](int quality, int rate, int policy) {
    stream->set_video_quality_index(quality);
    stream->set_frame_rate_index(rate);
    stream->set_adaptation_policy_index(policy);
    ++video_changes;
  });
  stream->on_select_file([&] { file_visible = true; });
  bool close_synchronously = false;
  stream->on_close_file_transfer([&] {
    // The real application hides this on its next synchronization tick.
    file_visible = false;
    if (close_synchronously) stream->set_file_transfer_visible(false);
  });
  screen.Settle();
  // New sessions start with only the logo. Reveal the toolbar through its
  // normal gesture before testing the buttons to the right of the logo.
  screen.Click(19, top + 19);
  constexpr int display_x = 43, shortcut_x = 75, file_x = 203, video_x = 235;
  for (int repeat = 0; repeat < 2; ++repeat) {
    screen.Click(display_x + 14, top + 19);
    if (!Check(screen.Light(display_x + 5, top + 128), "Display menu must fully open without pointer movement")) return false;
    screen.Click(display_x + 53, top + 94, 4);
    if (!Check(selected_display == 1 && screen.Black(display_x + 5, top + 128),
               "Display selection must survive live refresh and close the menu")) return false;
    screen.Click(shortcut_x + 14, top + 19);
    if (!Check(screen.Light(shortcut_x + 5, top + 102), "Shortcut menu must open without pointer movement")) return false;
    screen.Click(shortcut_x + 31, top + 94, 4);
    if (!Check(shortcut == "Win+L" && screen.Black(shortcut_x + 5, top + 102),
               "Shortcut selection must close the menu immediately")) return false;
    screen.Click(video_x + 14, top + 19);
    if (!Check(stream->get_video_menu_open() && screen.Light(video_x + 4, top + 210),
               "Video settings menu must fully open without pointer movement")) return false;
    screen.Click(video_x + 41, top + 80, 4);
    screen.Click(900, top + 300);
    if (!Check(!stream->get_video_menu_open() && video_changes == repeat + 1,
               "Video selection and outside dismissal must respond immediately")) return false;
  }
  if (!Check(remote_presses == 0 && remote_releases == 0 && remote_scrolls == 0,
             "Menu gestures must not be forwarded to the peer")) return false;
  screen.Click(file_x + 14, top + 19);
  const int panel_y = top + int(stream->get_video_area_height()) - 210;
  if (!Check(file_visible && screen.Light(20, panel_y + 25),
             "File progress must appear without pointer movement")) return false;
  // Header/padding is outside the list's own input handler.
  screen.Click(25, panel_y + 20, 4);
  stream->window().dispatch_pointer_scroll_event(
      slint::LogicalPosition({25, float(panel_y + 20)}), 0, -60);
  screen.Settle();
  if (!Check(remote_presses == 0 && remote_releases == 0 && remote_scrolls == 0,
             "File progress header and padding must consume input locally")) return false;
  const slint::LogicalPosition panel_point({25, float(panel_y + 20)});
  const slint::LogicalPosition outside({900, float(top + 300)});
  stream->window().dispatch_pointer_press_event(panel_point, slint::PointerEventButton::Left);
  screen.Settle(4);
  stream->window().dispatch_pointer_move_event(outside);
  stream->window().dispatch_pointer_release_event(outside, slint::PointerEventButton::Left);
  screen.Settle();
  if (!Check(remote_presses == 0 && remote_releases == 0,
             "Dragging out of the file panel must not release a button on the peer")) return false;
  screen.Click(430, panel_y + 16, 4);
  if (!Check(!file_visible && screen.Black(20, panel_y + 25),
             "File progress close must clear its pixels without pointer movement")) return false;
  if (!Check(remote_presses == 0 && remote_releases == 0,
             "Closing file progress must not send an orphan pointer release")) return false;
  close_synchronously = true;
  screen.Click(file_x + 14, top + 19);
  screen.Click(430, panel_y + 16, 4);
  if (!Check(!file_visible && screen.Black(20, panel_y + 25) && remote_releases == 0,
             "Synchronous file panel dismissal must consume its closing release")) return false;
  screen.Click(900, top + 300);
  return Check(remote_presses == 1 && remote_releases == 1,
               "Closing local menus must restore remote input");
}

bool VerifyWorkbenchPopups(RenderTestPlatform& platform) {
  auto main = CreateMainWindow();
  auto& screen = *platform.adapter;
  const float width = main->get_minimum_window_width();
  const float height = main->get_minimum_window_height();
  main->window().set_size(slint::LogicalSize({width, height}));
  main->show();
  auto groups = std::make_shared<slint::VectorModel<slint::SharedString>>(
      std::vector<slint::SharedString>{"All", "Ungrouped", "Lab"});
  main->set_recent_groups(groups);
  int selected_group = -1;
  main->on_recent_group_selected([&](int index) { selected_group = index; });
  screen.Settle();
  const auto filter_closed = screen.Region(width - 170, 302, 140, 70);
  screen.Click(width - 90, 276);
  if (!Check(screen.Region(width - 170, 302, 140, 70) != filter_closed,
             "Group filter must reveal its contents while the pointer stays on the trigger")) return false;
  screen.Key(slint::platform::key_codes::Escape);
  if (!Check(screen.Region(width - 170, 302, 140, 70) == filter_closed,
             "Escape must dismiss the group filter")) return false;
  screen.Click(width - 90, 276);
  screen.Click(width - 90, 371, 4);
  if (!Check(selected_group == 2 && main->get_recent_group_index() == 2,
             "Group selection must survive a held click")) return false;
  screen.Click(width - 38, 20);
  screen.Click(width - 78, 56, 4);
  if (!Check(main->get_settings_open(), "Application menu must open settings on click")) return false;
  const float settings_y = (height - 446) / 2;
  screen.Click(width / 2 + 87, settings_y + 76, 4);
  if (!Check(main->get_settings_tab() == 1, "Settings tabs must switch without pointer movement")) return false;
  screen.Click(width / 2 - 87, settings_y + 76, 4);
  if (!Check(main->get_settings_tab() == 0, "Settings tabs must switch back immediately")) return false;
  // The first row uses the same CompactComboBox as codec/capture settings.
  const int combo_x = int(width / 2 + 100);
  const int combo_y = int(settings_y + 150);
  const int popup_y = int(settings_y + 164);
  const auto combo_closed = screen.Region(combo_x - 35, popup_y + 2, 60, 60);
  screen.Click(combo_x, combo_y);
  if (!Check(screen.Region(combo_x - 35, popup_y + 2, 60, 60) != combo_closed,
             "Settings dropdown must animate open without pointer movement")) return false;
  screen.Key(slint::platform::key_codes::Escape);
  if (!Check(screen.Region(combo_x - 35, popup_y + 2, 60, 60) == combo_closed,
             "Escape must dismiss the settings dropdown")) return false;
  screen.Click(combo_x, combo_y);
  screen.Click(combo_x, popup_y + 34, 4);
  if (!Check(main->get_language_index() == 1, "Settings dropdown must apply a selected row")) return false;
  // A session starting while a codec menu is open disables the setting.
  // Existing popup rows must not remain actionable after that transition.
  const int codec_y = int(settings_y + 224);
  screen.Click(combo_x, codec_y);
  main->set_settings_session_active(true);
  screen.Settle();
  screen.Click(combo_x, codec_y + 48, 4);
  if (!Check(main->get_codec_index() == 0,
             "A disabled codec dropdown must close and reject its stale options")) return false;
  main->set_settings_session_active(false);
  screen.Click(width / 2 + 32, settings_y + 412);
  if (!Check(!main->get_settings_open(), "Settings Cancel must close the dialog immediately")) return false;
  screen.Click(width - 38, 20);
  screen.Click(width - 78, 86, 4);
  if (!Check(main->get_messages_open(), "Messages must open directly from the application menu")) return false;
  screen.Key(slint::platform::key_codes::Escape);
  if (!Check(!main->get_messages_open(), "Escape must dismiss Messages")) return false;
  screen.Click(width - 38, 20);
  screen.Click(width - 78, 132, 4);
  if (!Check(main->get_about_open(), "About must open directly from the application menu")) return false;
  screen.Key(slint::platform::key_codes::Escape);
  return Check(!main->get_about_open(), "Escape must dismiss About");
}

bool VerifyAutostartSettings(RenderTestPlatform& platform) {
  auto main = CreateMainWindow();
  auto& screen = *platform.adapter;
  using Status = crossdesk::ui::AutostartStatus;
  const float width = main->get_minimum_window_width();
  const float height = main->get_minimum_window_height();
  main->window().set_size(slint::LogicalSize({width, height}));
  main->set_autostart_system_managed(true);
  main->set_autostart_enabled(true); // Stale app preference must not label system state.
  main->set_autostart_status(Status::Disabled);
  int refreshes = 0;
  main->on_refresh_autostart_status([&] { ++refreshes; });
  main->show(); main->set_settings_open(true); screen.Settle();
  if (!Check(refreshes == 1, "Opening managed autostart settings must query the system")) return false;
  const float top = (height - 446) / 2;
  main->window().dispatch_pointer_scroll_event(slint::LogicalPosition({width / 2, top + 260}), 0, -1200);
  screen.Settle();
  const float toggle_x = width / 2 + 144, toggle_y = top + 322;
  const auto toggle_pixels = [&] {
    return screen.Region(int(toggle_x - 18), int(toggle_y - 10), 36, 20);
  };
  const auto save_snapshot = [&](const std::string& suffix) {
    if (const char* snapshot = std::getenv("CROSSDESK_AUTOSTART_UI_SNAPSHOT")) {
      std::ofstream out(std::string(snapshot) + suffix, std::ios::binary);
      out << "P6\n" << screen.extent.width << ' ' << screen.extent.height << "\n255\n";
      for (const auto pixel : screen.pixels) { out.put(pixel.r); out.put(pixel.g); out.put(pixel.b); }
    }
  };
  const auto disabled = toggle_pixels();
  save_snapshot(".settings.ppm");
  main->set_autostart_status(Status::Enabled); screen.Settle();
  const auto enabled = toggle_pixels();
  if (!Check(enabled != disabled,
             "The autostart switch must reflect system changes while settings stay open")) return false;
  main->set_autostart_status(Status::Disabled); screen.Settle();
  screen.Click(toggle_x, toggle_y);
  if (!Check(main->get_autostart_guide_open() && main->get_autostart_status() == Status::Disabled &&
                 main->get_autostart_enabled(),
             "Clicking the disabled system switch opens guidance without changing state or preference")) return false;
  screen.Key(slint::platform::key_codes::Escape);
  if (!Check(toggle_pixels() == disabled, "The system switch must not flip when guidance closes")) return false;
  screen.Click(toggle_x, toggle_y);
  // Both actions stay below the scrollable instructions at minimum size.
  const auto button_y = [&]() {
    const int x = int(width / 2 + 120), y = int(height / 2 + 158);
    const auto p = screen.pixels.at(y * screen.extent.width + x);
    return p.b > 180 && p.r < 90 && p.g > 80 && p.g < 160 ? y : -1;
  };
  int refresh_y = button_y();
  if (!Check(refresh_y > 0, "Autostart guide buttons must fit inside the minimum window")) return false;
  save_snapshot("");
  screen.Click(width / 2 - 145, refresh_y);
  if (!Check(refreshes == 2, "Autostart guide Refresh must query the actual system")) return false;
  main->set_autostart_status(Status::Checking); screen.Settle();
  screen.Click(width / 2 - 145, button_y());
  if (!Check(refreshes == 2, "Refresh is disabled while the system query is pending")) return false;
  for (const auto status : {Status::Failed, Status::Unsupported, Status::Enabled}) {
    main->set_autostart_status(status); screen.Settle();
    if (!Check(button_y() > 0, "Error and unsupported states must remain readable and dismissible")) return false;
  }
  screen.Key(slint::platform::key_codes::Escape);
  if (!Check(!main->get_autostart_guide_open() && main->get_settings_open(),
             "Escape closes the guide without cancelling other settings")) return false;
  if (!Check(toggle_pixels() == enabled, "The switch must still follow system state after opening guidance")) return false;
  screen.Click(toggle_x, toggle_y);
  if (!Check(main->get_autostart_guide_open() && main->get_autostart_status() == Status::Enabled,
             "Clicking the enabled system switch must also open guidance without changing state")) return false;
  screen.Click(width / 2 + 160, button_y());
  if (!Check(!main->get_autostart_guide_open(), "The guide confirmation must close immediately")) return false;
  if (!Check(toggle_pixels() == enabled, "Dismissing guidance must keep the system switch enabled")) return false;
  main->set_autostart_guide_open(true); main->set_settings_open(false); screen.Settle();
  if (!Check(!main->get_autostart_guide_open(), "Closing settings must also dismiss its guide")) return false;
  main->set_autostart_system_managed(false);
  main->set_settings_open(true); screen.Settle();
  main->window().dispatch_pointer_scroll_event(slint::LogicalPosition({width / 2, top + 260}), 0, -1200);
  screen.Settle();
  screen.Click(toggle_x, toggle_y);
  return Check(refreshes == 2 && !main->get_autostart_enabled() && !main->get_autostart_guide_open(),
               "Other platforms toggle their autostart preference directly without querying Harmony or opening guidance");
}

bool VerifyGalleryPopups(RenderTestPlatform& platform) {
  auto wall = CreateMainWindow();
  auto& screen = *platform.adapter;
  wall->set_gallery_mode(true);
  wall->window().set_size(slint::LogicalSize({900, 600}));
  wall->set_gallery_count(8);
  wall->set_gallery_connected(8);
  wall->set_gallery_groups(std::make_shared<slint::VectorModel<slint::SharedString>>(
      std::vector<slint::SharedString>{"All", "Lab"}));
  auto network = std::make_shared<slint::VectorModel<crossdesk::ui::GalleryNetworkDevice>>();
  wall->set_gallery_network_devices(network);
  wall->show();
  unsigned ticks = 0;
  slint::Timer sync;
  sync.start(slint::TimerMode::Repeated, 16ms, [&] {
    crossdesk::ui::GalleryNetworkDevice row{};
    row.id = "123456789"; row.name = "Test device";
    row.inbound = slint::SharedString(std::to_string(++ticks) + " kbps");
    crossdesk::ui::UpdateModelRows(*network, std::vector(8, row));
  });
  screen.Settle();
  const auto baseline = screen.Region(75, 100, 740, 400);
  for (int repeat = 0; repeat < 2; ++repeat) {
    screen.Click(160, 578);
    if (!Check(wall->get_gallery_network_open() && screen.Region(75, 100, 740, 400) != baseline,
               "DeskHub network dialog must paint immediately during live refresh")) return false;
    // Eight rows fill the available height, placing the dialog 16 px from the top.
    if (repeat == 0) screen.Click(780, 46, 4);
    else screen.Key(slint::platform::key_codes::Escape);
    if (!Check(!wall->get_gallery_network_open() && screen.Region(75, 100, 740, 400) == baseline,
               "DeskHub network dialog must close completely without pointer movement")) return false;
  }
  // Open the add menu, leave the pointer on it, then select its first row.
  const auto menu_closed = screen.Region(490, 42, 220, 60);
  screen.Click(680, 24);
  if (!Check(screen.Region(490, 42, 220, 60) != menu_closed,
             "DeskHub add menu must appear without pointer movement")) return false;
  screen.Click(600, 55, 4);
  if (!Check(wall->get_gallery_adding(), "Add menu must open the device dialog")) return false;
  screen.Click(422, 421, 4);
  return Check(!wall->get_gallery_adding(), "Add dialog Cancel must respond immediately");
}

bool VerifyUpdateRecovery(RenderTestPlatform& platform) {
  auto main = CreateMainWindow();
  auto& screen = *platform.adapter;
  const float width = main->get_minimum_window_width();
  const float height = main->get_minimum_window_height();
  main->window().set_size(slint::LogicalSize({width, height}));
  main->set_update_available(true);
  main->set_update_install_available(true);
  main->set_latest_version("v2.0.0");
  int install_requests = 0, manual_downloads = 0;
  main->on_open_download([&] { ++install_requests; main->set_update_confirm_open(true); });
  main->on_open_manual_download([&] { ++manual_downloads; });
  main->show();
  screen.Settle();
  const float button_y = height / 2 + std::min(488.f, height - 32) / 2 - 41;
  const float action_x = width / 2 + 161;
  const float manual_x = action_x - 118;
  const auto action_pixels = [&] {
    return screen.Region(int(action_x - 40), int(button_y - 12), 80, 24);
  };
  const auto install_label = action_pixels();
  main->set_update_install_failed(true);
  main->set_update_install_status(crossdesk::ui_localization::Text(
      crossdesk::localization::update_install_failed[0] + "\nDownload failed"));
  screen.Settle();
  if (!Check(action_pixels() != install_label,
             "A failed update must visibly replace Install update with Retry update")) return false;
  screen.Click(manual_x, button_y);
  if (!Check(manual_downloads == 1 && install_requests == 0 && !main->get_update_confirm_open(),
             "Manual download must remain reachable after failure without starting automatic installation")) return false;
  screen.Click(action_x, button_y);
  if (!Check(install_requests == 1 && main->get_update_confirm_open(),
             "Retry must open the installation confirmation")) return false;
  screen.Key(slint::platform::key_codes::Escape);
  if (!Check(!main->get_update_confirm_open(), "Retry confirmation must remain cancellable")) return false;
  main->set_update_install_busy(true);
  screen.Settle();
  screen.Click(action_x, button_y);
  screen.Click(manual_x, button_y);
  if (!Check(install_requests == 1 && manual_downloads == 1,
             "Recovery buttons must reject repeated clicks while an update is running")) return false;
  main->set_update_install_busy(false);
  main->set_update_install_available(false);
  main->set_update_install_status(crossdesk::ui_localization::Text(
      crossdesk::localization::update_install_failed_manual[0]));
  // The application's primary action falls back to manual download when the
  // current installation or signed manifest no longer supports automatic updates.
  main->on_open_download([&] { ++manual_downloads; });
  screen.Settle();
  screen.Click(action_x, button_y);
  return Check(manual_downloads == 2 && !main->get_update_confirm_open(),
               "An unavailable automatic retry must retain the primary manual download action");
}

bool VerifyServerPanel(RenderTestPlatform& platform) {
  auto panel = crossdesk::ui::ServerWindow::create();
  auto& screen = *platform.adapter;
  panel->set_controllers(std::make_shared<slint::VectorModel<crossdesk::ui::ControllerEntry>>(
      std::vector<crossdesk::ui::ControllerEntry>{{"1", "First"}, {"2", "Second"}}));
  panel->window().set_size(slint::LogicalSize({225, panel->get_content_height()}));
  panel->show();
  int selected = -1, disconnected = 0, picked = 0;
  panel->on_controller_selected([&](int index) { selected = index; });
  panel->on_disconnect_all([&] { ++disconnected; });
  panel->on_select_file([&] { ++picked; panel->set_file_transfer_visible(true); });
  slint::Timer sync;
  sync.start(slint::TimerMode::Repeated, 16ms, [&] {
    const auto height = unsigned(panel->get_content_height());
    if (screen.extent.height != height)
      panel->window().set_size(slint::LogicalSize({225, float(height)}));
    panel->set_file_progress(0.5);
  });
  screen.Settle();
  screen.Click(50, 84, 4);
  if (!Check(selected == 1, "Controlled-side controller selection must respond to a held click")) return false;
  const auto normal_height = screen.extent.height;
  screen.Click(180, 123, 4);
  if (!Check(panel->get_confirming_disconnect() && screen.extent.height == normal_height + 36,
             "Disconnect confirmation must expand immediately")) return false;
  screen.Click(110, 123, 4);
  if (!Check(!panel->get_confirming_disconnect() && disconnected == 0 && screen.extent.height == normal_height,
             "Cancel must collapse the controlled-side confirmation")) return false;
  screen.Click(180, 123, 4);
  screen.Click(180, 123, 4);
  if (!Check(disconnected == 1 && !panel->get_confirming_disconnect(),
             "Disconnect all must require and accept confirmation")) return false;
  screen.Click(110, 123, 4);
  return Check(picked == 1 && screen.extent.height == normal_height + 58,
               "Controlled-side file progress must resize the panel immediately");
}
}  // namespace

int main() {
  auto platform = std::make_unique<RenderTestPlatform>();
  auto* backend = platform.get();
  slint::platform::set_platform(std::move(platform));
  if (!VerifyStreamPopups(*backend)) return 1;
  if (!VerifyWorkbenchPopups(*backend)) return 2;
  if (!VerifyGalleryPopups(*backend)) return 3;
  if (!VerifyServerPanel(*backend)) return 4;
  if (!VerifyAutostartSettings(*backend)) return 5;
  if (!VerifyUpdateRecovery(*backend)) return 6;
  std::cout << "Slint popup rendering and local input passed.\n";
}
