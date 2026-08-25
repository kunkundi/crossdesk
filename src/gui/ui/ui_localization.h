#ifndef CROSSDESK_GUI_UI_UI_LOCALIZATION_H_
#define CROSSDESK_GUI_UI_UI_LOCALIZATION_H_

#include <slint.h>

#include <string>

#include "crossdesk_ui.h"
#include "localization.h"

namespace crossdesk::ui_localization {

inline slint::SharedString Text(const std::string& value) {
  return slint::SharedString(value);
}

inline int ApplyMainWindowStrings(
    const slint::ComponentHandle<ui::MainWindow>& window,
    int language_index) {
  const int language =
      localization::detail::ClampLanguageIndex(language_index);
  auto& strings = window->global<ui::UiStrings>();
  strings.set_local_desktop(Text(localization::local_desktop[language]));
  strings.set_remote_desktop(Text(localization::remote_desktop[language]));
  strings.set_recent_connections(
      Text(localization::recent_connections[language]));
  strings.set_local_id(Text(localization::local_id[language]));
  strings.set_device_name(Text(localization::device_name[language]));
  strings.set_remote_id(Text(localization::remote_id[language]));
  strings.set_online(Text(localization::online[language]));
  strings.set_offline(Text(localization::offline[language]));
  strings.set_password(Text(localization::password[language]));
  strings.set_copied(
      Text(localization::local_id_copied_to_clipboard[language]));
  strings.set_connect(Text(localization::connect[language]));
  strings.set_settings(Text(localization::settings[language]));
  strings.set_about(Text(localization::about[language]));
  strings.set_ok(Text(localization::ok[language]));
  strings.set_cancel(Text(localization::cancel[language]));
  strings.set_new_password(Text(localization::new_password[language]));
  strings.set_invalid_password(Text(localization::max_password_len[language]));
  strings.set_edit_alias(
      Text(localization::input_connection_alias[language]));
  strings.set_delete_connection(
      Text(localization::delete_connection[language]));
  strings.set_confirm_delete(
      Text(localization::confirm_delete_connection[language]));
  strings.set_language(Text(localization::language[language]));
  strings.set_video_quality(Text(localization::video_quality[language]));
  strings.set_frame_rate(Text(localization::video_frame_rate[language]));
  strings.set_codec(Text(localization::video_encode_format[language]));
  strings.set_hardware_codec(
      Text(localization::enable_hardware_video_codec[language]));
  strings.set_turn_relay(Text(localization::enable_turn[language]));
  strings.set_srtp(Text(localization::enable_srtp[language]));
  strings.set_self_hosted(
      Text(localization::self_hosted_server_config[language]));
  strings.set_autostart(Text(localization::enable_autostart[language]));
  strings.set_daemon(Text(localization::enable_daemon[language]));
  strings.set_file_save_path(
      Text(localization::file_transfer_save_path[language]));
  strings.set_default_desktop(Text(localization::default_desktop[language]));
  strings.set_server_host(
      Text(localization::self_hosted_server_address[language]));
  strings.set_server_port(
      Text(localization::self_hosted_server_port[language]));
  strings.set_coturn_port(
      Text(localization::self_hosted_server_coturn_server_port[language]));
  strings.set_version(Text(localization::version[language]));
  strings.set_signal_connected(Text(localization::signal_connected[language]));
  strings.set_signal_disconnected(
      Text(localization::signal_disconnected[language]));
  strings.set_tls_error(Text(localization::signal_tls_cert_error[language]));
  strings.set_update_available(
      Text(localization::new_version_available[language]));
  strings.set_release_notes(Text(localization::release_notes[language]));
  strings.set_download(Text(localization::update[language]));
  strings.set_input_password(Text(localization::input_password[language]));
  strings.set_reinput_password(Text(localization::reinput_password[language]));
  strings.set_remember_password(
      Text(localization::remember_password[language]));
  strings.set_validate_password(
      Text(localization::validate_password[language]));
  strings.set_request_permissions(
      Text(localization::request_permissions[language]));
  strings.set_permission_required(
      Text(localization::permission_required_message[language]));
  strings.set_screen_recording_permission(
      Text(localization::screen_recording_permission[language]));
  strings.set_accessibility_permission(
      Text(localization::accessibility_permission[language]));
  strings.set_service_setup_title(
      Text(localization::windows_service_setup_title[language]));
  strings.set_service_setup_message(
      Text(localization::windows_service_setup_message[language]));
  strings.set_service_settings_label(
      Text(localization::windows_service_settings_label[language]));
  strings.set_install_service(
      Text(localization::install_windows_service[language]));
  strings.set_service_installed(
      Text(localization::windows_service_installed[language]));
  strings.set_do_not_remind(
      Text(localization::do_not_remind_again[language]));
  strings.set_notification(Text(localization::notification[language]));
  strings.set_service_suppressed_message(
      Text(localization::windows_service_prompt_suppressed_message[language]));
  strings.set_quality_low(Text(localization::video_quality_low[language]));
  strings.set_quality_medium(
      Text(localization::video_quality_medium[language]));
  strings.set_quality_high(Text(localization::video_quality_high[language]));
  strings.set_codec_h264(Text(localization::h264[language]));
  strings.set_codec_av1(Text(localization::av1[language]));
  strings.set_self_hosted_settings(
      Text(localization::self_hosted_server_settings[language]));
  strings.set_access_website(Text(localization::access_website[language]));
  strings.set_release_date_label(Text(localization::release_date[language]));
  strings.set_later(Text(localization::cancel[language]));
  return language;
}

template <typename Window>
inline int ApplyStreamWindowStrings(
    const slint::ComponentHandle<Window>& window, int language_index) {
  const int language =
      localization::detail::ClampLanguageIndex(language_index);
  auto& strings = window->template global<ui::StreamStrings>();
  strings.set_select_display(Text(localization::select_display[language]));
  strings.set_send_shortcut(Text(localization::send_shortcut[language]));
  strings.set_control_mouse(Text(localization::control_mouse[language]));
  strings.set_release_mouse(Text(localization::release_mouse[language]));
  strings.set_audio(Text(localization::audio_capture[language]));
  strings.set_mute(Text(localization::mute[language]));
  strings.set_select_file(Text(localization::select_file[language]));
  strings.set_show_stats(
      Text(localization::show_net_traffic_stats[language]));
  strings.set_hide_stats(
      Text(localization::hide_net_traffic_stats[language]));
  strings.set_fullscreen(Text(localization::fullscreen[language]));
  strings.set_exit_fullscreen(Text(localization::exit_fullscreen[language]));
  strings.set_disconnect(Text(localization::disconnect[language]));
  strings.set_file_transfer(
      Text(localization::file_transfer_progress[language]));
  strings.set_expand_control(Text(localization::expand_control_bar[language]));
  strings.set_collapse_control(
      Text(localization::collapse_control_bar[language]));
  strings.set_stats_in(Text(localization::in[language]));
  strings.set_stats_out(Text(localization::out[language]));
  strings.set_stats_loss_rate(Text(localization::loss_rate[language]));
  strings.set_stats_resolution(Text(localization::resolution[language]));
  strings.set_stats_connection_mode(
      Text(localization::connection_mode[language]));
  return language;
}

}  // namespace crossdesk::ui_localization

#endif  // CROSSDESK_GUI_UI_UI_LOCALIZATION_H_
