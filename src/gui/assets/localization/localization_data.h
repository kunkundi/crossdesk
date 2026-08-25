/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */
#ifndef _LOCALIZATION_DATA_H_
#define _LOCALIZATION_DATA_H_

namespace crossdesk {
namespace localization {

namespace detail {

struct TranslationRow {
  const char* key;
  const void* zh;
  const void* en;
  const void* ru;
};

// Single source of truth for all UI strings.
#define CROSSDESK_LOCALIZATION_ALL(X)                                          \
  X(local_desktop, u8"本桌面", "Local Desktop", u8"Локальный рабочий стол")    \
  X(local_id, u8"本机ID", "Local ID", u8"Локальный ID")                        \
  X(local_id_copied_to_clipboard, u8"已复制到剪贴板", "Copied to clipboard",   \
    u8"Скопировано в буфер обмена")                                            \
  X(password, u8"密码", "Password", u8"Пароль")                                \
  X(max_password_len, u8"最大6个字符", "Max 6 chars", u8"Макс. 6 символов")    \
  X(remote_desktop, u8"远程桌面", "Remote Desktop",                            \
    u8"Удаленный рабочий стол")                                                \
  X(device_name, u8"设备名称", "Device Name", u8"Имя устройства")             \
  X(remote_id, u8"对端ID", "Remote ID", u8"Удаленный ID")                      \
  X(connect, u8"连接", "Connect", u8"Подключиться")                            \
  X(recent_connections, u8"近期连接", "Recent Connections",                    \
    u8"Недавние подключения")                                                  \
  X(disconnect, u8"断开连接", "Disconnect", u8"Отключить")                     \
  X(select_display, u8"选择显示器", "Select Display", u8"Выбрать дисплей")     \
  X(display_screen, u8"显示屏", "Display", u8"Экран")                        \
  X(expand_control_bar, u8"展开控制栏", "Expand Control Bar",                  \
    u8"Развернуть панель управления")                                          \
  X(collapse_control_bar, u8"收起控制栏", "Collapse Control Bar",              \
    u8"Свернуть панель управления")                                            \
  X(fullscreen, u8"全屏", " Fullscreen", u8"Полный экран")                     \
  X(show_net_traffic_stats, u8"显示网络状态", "Show Net Traffic Stats",        \
    u8"Показать статистику трафика")                                           \
  X(hide_net_traffic_stats, u8"隐藏网络状态", "Hide Net Traffic Stats",        \
    u8"Скрыть статистику трафика")                                             \
  X(video, u8"视频", "Video", u8"Видео")                                       \
  X(audio, u8"音频", "Audio", u8"Аудио")                                       \
  X(data, u8"数据", "Data", u8"Данные")                                        \
  X(total, u8"总计", "Total", u8"Итого")                                       \
  X(in, u8"输入", "In", u8"Вход")                                              \
  X(out, u8"输出", "Out", u8"Выход")                                           \
  X(loss_rate, u8"丢包率", "Loss Rate", u8"Потери пакетов")                    \
  X(exit_fullscreen, u8"退出全屏", "Exit fullscreen",                          \
    u8"Выйти из полноэкранного режима")                                        \
  X(control_mouse, u8"控制鼠标", "Control Mouse", u8"Управление мышью")        \
  X(release_mouse, u8"释放鼠标", "Release Mouse", u8"Освободить мышь")         \
  X(audio_capture, u8"播放声音", "Audio Capture", u8"Воспроизведение звука")   \
  X(mute, u8" 静音", " Mute", u8"Без звука")                                   \
  X(send_shortcut, u8"发送组合键", "Send Shortcut", u8"Сочетания клавиш")      \
  X(send_sas, u8"发送SAS", "Send SAS", u8"Отправить SAS")                      \
  X(lock_remote, u8"锁定远端", "Lock Remote", u8"Заблокировать")               \
  X(remote_password_box_visible, u8"远端密码框已出现",                         \
    "Remote password box visible", u8"Окно ввода пароля видно")                \
  X(remote_lock_screen_hint, u8"远端处于锁屏封面，可发送SAS",                  \
    "Remote lock screen visible, send SAS",                                    \
    u8"Видна блокировка, отправьте SAS")                                       \
  X(remote_secure_desktop_active, u8"远端已进入安全桌面",                      \
    "Remote secure desktop active", u8"Активен защищенный рабочий стол")       \
  X(remote_service_unavailable, u8"远端Windows服务不可用",                     \
    "Remote Windows service unavailable",                                      \
    u8"Служба Windows на удаленной стороне недоступна")                        \
  X(windows_service_setup_title, u8"安装 CrossDesk Service",                   \
    "Install CrossDesk Service", u8"Установить CrossDesk Service")             \
  X(windows_service_setup_message,                                             \
    u8"为支持该设备在锁屏状态下被远程控制，需要以管理员权限安装 CrossDesk "    \
    u8"Service。\n未安装该服务不影响 CrossDesk "                               \
    u8"正常使用，仅无法在锁屏状态下控制本机。",                                \
    "To support remote control of this device while it is locked, CrossDesk "  \
    "Service must be installed with administrator permission.\nWithout this "  \
    "service, CrossDesk still works normally; only lock-screen control of "    \
    "this computer is unavailable.",                                           \
    u8"Чтобы поддерживать удаленное управление этим устройством на экране "    \
    u8"блокировки, необходимо установить CrossDesk Service с правами "         \
    u8"администратора.\nБез этой службы CrossDesk продолжит работать "         \
    u8"нормально; будет недоступно только управление этим компьютером на "     \
    u8"экране блокировки.")                                                    \
  X(install_windows_service, u8"安装", "Install", u8"Установить")              \
  X(windows_service_settings_label, u8"锁屏控制服务:",                         \
    "Lock Screen Service:", u8"Служба блокировки экрана:")                     \
  X(windows_service_installed, u8"已安装", "Installed", u8"Установлена")       \
  X(do_not_remind_again, u8"不再提醒", "Do not remind again",                  \
    u8"Больше не напоминать")                                                  \
  X(windows_service_prompt_suppressed_message,                                 \
    u8"已不再提醒。后续如需启用锁屏状态下被远程控制，可在设置中点击“安装”。",  \
    "You will not be reminded again. To enable remote control while locked "   \
    "later, click Install in Settings.",                                       \
    u8"Напоминание отключено. Чтобы позже включить удаленное управление на "   \
    u8"экране блокировки, нажмите «Установить» в настройках.")                 \
  X(installing_windows_service, u8"正在安装服务...", "Installing service...",  \
    u8"Установка службы...")                                                   \
  X(windows_service_install_success, u8"服务已安装并启动",                     \
    "Service installed and started", u8"Служба установлена и запущена")        \
  X(windows_service_install_failed,                                            \
    u8"服务安装失败。请确认 "                                                  \
    u8"CrossDesk.exe、crossdesk_service.exe、crossdesk_session_helper.exe "    \
    u8"位于同一便携目录中，并在系统弹窗中允许管理员权限。",                    \
    "Service installation failed. Make sure CrossDesk.exe, "                   \
    "crossdesk_service.exe, and crossdesk_session_helper.exe are in the same " \
    "portable folder, then approve the administrator prompt.",                 \
    u8"Не удалось установить службу. Убедитесь, что CrossDesk.exe, "           \
    u8"crossdesk_service.exe и crossdesk_session_helper.exe находятся в "      \
    u8"одной папке портативной версии, затем подтвердите запрос прав "         \
    u8"администратора.")                                                       \
  X(remote_unlock_requires_secure_desktop,                                     \
    u8"当前仍需要安全桌面专用采集/输入",                                       \
    "Secure desktop capture/input is still required",                          \
    u8"По-прежнему нужен отдельный захват/ввод для защищенного рабочего "      \
    u8"стола")                                                                 \
  X(settings, u8"设置", "Settings", u8"Настройки")                             \
  X(language, u8"语言:", "Language:", u8"Язык:")                               \
  X(video_quality, u8"视频质量:", "Video Quality:", u8"Качество видео:")       \
  X(video_frame_rate, u8"画面采集帧率:",                                       \
    "Video Capture Frame Rate:", u8"Частота захвата видео:")                   \
  X(video_quality_high, u8"高", "High", u8"Высокое")                           \
  X(video_quality_medium, u8"中", "Medium", u8"Среднее")                       \
  X(video_quality_low, u8"低", "Low", u8"Низкое")                              \
  X(video_encode_format, u8"视频编码格式:",                                    \
    "Video Encode Format:", u8"Формат кодека видео:")                          \
  X(av1, u8"AV1", "AV1", "AV1")                                                \
  X(h264, u8"H.264", "H.264", "H.264")                                         \
  X(enable_hardware_video_codec, u8"启用硬件编解码器:",                        \
    "Enable Hardware Video Codec:", u8"Использовать аппаратный кодек:")        \
  X(enable_turn, u8"启用中继服务:",                                            \
    "Enable TURN Service:", u8"Включить TURN-сервис:")                         \
  X(enable_srtp, u8"启用SRTP:", "Enable SRTP:", u8"Включить SRTP:")            \
  X(self_hosted_server_config, u8"自托管配置", "Self-Hosted Config",           \
    u8"Конфигурация self-hosted")                                              \
  X(self_hosted_server_settings, u8"自托管设置", "Self-Hosted Settings",       \
    u8"Настройки self-hosted")                                                 \
  X(self_hosted_server_address, u8"服务器地址:",                               \
    "Server Address:", u8"Адрес сервера:")                                     \
  X(self_hosted_server_port, u8"信令服务端口:",                                \
    "Signal Service Port:", u8"Порт сигнального сервиса:")                     \
  X(self_hosted_server_coturn_server_port, u8"中继服务端口:",                  \
    "Relay Service Port:", u8"Порт реле-сервиса:")                             \
  X(ok, u8"确认", "OK", u8"ОК")                                                \
  X(cancel, u8"取消", "Cancel", u8"Отмена")                                    \
  X(new_password, u8"请输入六位密码:",                                         \
    "Please input a six-char password:", u8"Введите шестизначный пароль:")     \
  X(input_password, u8"请输入密码:",                                           \
    "Please input password:", u8"Введите пароль:")                             \
  X(validate_password, u8"验证密码中...", "Validate password ...",             \
    u8"Проверка пароля...")                                                    \
  X(reinput_password, u8"请重新输入密码", "Please input password again",       \
    u8"Повторно введите пароль")                                               \
  X(remember_password, u8"记住密码", "Remember password",                      \
    u8"Запомнить пароль")                                                      \
  X(signal_connected, u8"已连接服务器", "Connected", u8"Подключено к серверу") \
  X(signal_disconnected, u8"未连接服务器", "Disconnected",                     \
    u8"Нет подключения к серверу")                                             \
  X(signal_tls_cert_error, u8"证书验证失败，请重新安装自托管根证书",           \
    "Certificate verification failed. Reinstall the self-hosted root "         \
    "certificate.",                                                            \
    u8"Ошибка проверки сертификата. Переустановите корневой сертификат.")      \
  X(p2p_connected, u8"对等连接已建立", "P2P Connected", u8"P2P подключено")    \
  X(p2p_disconnected, u8"对等连接已断开", "P2P Disconnected",                  \
    u8"P2P отключено")                                                         \
  X(p2p_connecting, u8"正在建立对等连接...", "P2P Connecting ...",             \
    u8"Подключение P2P...")                                                    \
  X(p2p_gathering, u8"正在收集候选地址...", "Gathering candidates ...",         \
    u8"Сбор кандидатов...")                                                    \
  X(receiving_screen, u8"画面接收中...", "Receiving screen...",                \
    u8"Получение изображения...")                                              \
  X(p2p_failed, u8"对等连接失败", "P2P Failed", u8"Сбой P2P")                  \
  X(p2p_closed, u8"对等连接已关闭", "P2P closed", u8"P2P закрыто")             \
  X(no_such_id, u8"无此ID", "No such ID", u8"ID не найден")                    \
  X(about, u8"关于", "About", u8"О программе")                                 \
  X(notification, u8"通知", "Notification", u8"Уведомление")                   \
  X(new_version_available, u8"新版本可用", "New Version Available",            \
    u8"Доступна новая версия")                                                 \
  X(release_notes, u8"更新内容", "Release Notes", u8"Содержание обновления") \
  X(version, u8"版本", "Version", u8"Версия")                                  \
  X(release_date, u8"发布日期: ", "Release Date: ", u8"Дата релиза: ")         \
  X(access_website, u8"访问官网: ",                                            \
    "Access Website: ", u8"Официальный сайт: ")                                \
  X(update, u8"更新", "Update", u8"Обновить")                                  \
  X(connection_alias, u8"修改名称", "Edit Alias",                              \
    u8"Изменить имя подключения")                                              \
  X(delete_connection, u8"删除连接", "Delete Connection",                      \
    u8"Удалить подключение")                                                   \
  X(connect_to_this_connection, u8"发起连接", "Connect to this connection",    \
    u8"Подключиться")                                                          \
  X(input_connection_alias, u8"请输入连接名称:",                               \
    "Please input connection name:", u8"Введите имя подключения:")             \
  X(confirm_delete_connection, u8"确认删除此连接",                             \
    "Confirm to delete this connection", u8"Удалить это подключение?")         \
  X(enable_autostart, u8"开机自启:", "Auto Start:", u8"Автозапуск:")           \
  X(enable_daemon, u8"启用守护进程:", "Enable Daemon:", u8"Включить демон:")   \
  X(takes_effect_after_restart, u8"重启后生效", "Takes effect after restart",  \
    u8"Вступит в силу после перезапуска")                                      \
  X(select_file, u8"选择文件发送", "Select File to Send",                      \
    u8"Выбрать файл для отправки")                                             \
  X(file_transfer_progress, u8"文件传输进度", "File Transfer Progress",        \
    u8"Прогресс передачи файлов")                                              \
  X(queued, u8"队列中", "Queued", u8"В очереди")                               \
  X(sending, u8"正在传输", "Sending", u8"Передача")                            \
  X(completed, u8"已完成", "Completed", u8"Завершено")                         \
  X(failed, u8"失败", "Failed", u8"Ошибка")                                    \
  X(controller, u8"控制端:", "Controller:", u8"Контроллер:")                   \
  X(file_transfer, u8"文件传输:", "File Transfer:", u8"Передача файлов:")      \
  X(connection_status, u8"连接状态:",                                          \
    "Connection Status:", u8"Состояние соединения:")                           \
  X(file_transfer_save_path, u8"文件接收保存路径:",                            \
    "File Transfer Save Path:", u8"Путь сохранения файлов:")                   \
  X(default_desktop, u8"桌面", "Desktop", u8"Рабочий стол")                    \
  X(resolution, u8"分辨率", "Res", u8"Разрешение")                             \
  X(connection_mode, u8"连接模式", "Mode", u8"Режим")                          \
  X(connection_mode_direct, u8"直连", "Direct", u8"Прямой")                    \
  X(connection_mode_relay, u8"中继", "Relay", u8"Релейный")                    \
  X(online, u8"在线", "Online", u8"Онлайн")                                    \
  X(offline, u8"离线", "Offline", u8"Офлайн")                                  \
  X(device_offline, u8"设备离线", "Device Offline", u8"Устройство офлайн")     \
  X(request_permissions, u8"权限请求", "Request Permissions",                  \
    u8"Запрос разрешений")                                                     \
  X(screen_recording_permission, u8"屏幕录制权限",                             \
    "Screen Recording Permission", u8"Разрешение на запись экрана")            \
  X(accessibility_permission, u8"辅助功能权限", "Accessibility Permission",    \
    u8"Разрешение специальных возможностей")                                   \
  X(permission_required_message, u8"该应用需要授权以下权限:",                  \
    "The application requires the following permissions:",                     \
    u8"Для работы приложения требуются следующие разрешения:")                 \
  X(show_main_window, u8"显示主界面", "Show Main Window",                    \
    u8"Показать главное окно")                                                \
  X(exit_program, u8"退出", "Exit", u8"Выход")

inline constexpr TranslationRow kTranslationRows[] = {
#define CROSSDESK_DECLARE_TRANSLATION_ROW(name, zh, en, ru) {#name, zh, en, ru},
    CROSSDESK_LOCALIZATION_ALL(CROSSDESK_DECLARE_TRANSLATION_ROW)
#undef CROSSDESK_DECLARE_TRANSLATION_ROW
};

}  // namespace detail

}  // namespace localization
}  // namespace crossdesk

#endif
