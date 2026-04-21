#ifndef _CROSSDESK_SERVICE_HOST_H_
#define _CROSSDESK_SERVICE_HOST_H_

#include <Windows.h>

#include <mutex>
#include <string>
#include <thread>

namespace crossdesk {

inline constexpr wchar_t kCrossDeskServiceName[] = L"CrossDeskService";
inline constexpr wchar_t kCrossDeskServiceDisplayName[] = L"CrossDesk Service";
inline constexpr wchar_t kCrossDeskServicePipeName[] =
    L"\\\\.\\pipe\\CrossDeskService";

class CrossDeskServiceHost {
 public:
  CrossDeskServiceHost();
  ~CrossDeskServiceHost();

  int RunAsService();
  int RunInConsole();

 private:
  int RunServiceLoop(bool as_service);
  int InitializeRuntime();
  void ShutdownRuntime();
  void RequestStop();
  void ReportServiceStatus(DWORD current_state, DWORD win32_exit_code,
                           DWORD wait_hint);
  void IpcServerLoop();
  void RefreshSessionState();
  void EnsureSessionHelper();
  void ReapSessionHelper();
  void StopSessionHelper();
  bool LaunchSessionHelper(DWORD session_id);
  void ReapSecureInputHelper();
  void StopSecureInputHelper();
  bool LaunchSecureInputHelper(DWORD session_id);
  std::wstring GetSessionHelperPath() const;
  std::wstring GetSessionHelperStopEventName(DWORD session_id) const;
  std::wstring GetSecureInputHelperPath() const;
  std::wstring GetSecureInputHelperStopEventName(DWORD session_id) const;
  void ResetSessionHelperReportedStateLocked(const char* error,
                                             DWORD error_code);
  bool GetEffectiveSessionLockedLocked() const;
  bool IsHelperReportingLockScreenLocked() const;
  bool HasSecureInputUiLocked() const;
  bool ShouldKeepSecureInputHelperLocked(DWORD target_session_id) const;
  void RefreshSessionHelperReportedState();
  void RecordSessionEvent(DWORD event_type, DWORD session_id);
  std::string HandleIpcCommand(const std::string& command);
  std::string BuildStatusResponse();
  std::string SendSecureAttentionSequence();
  std::string SendSecureDesktopKeyboardInput(int key_code, bool is_down);

  static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
  static BOOL WINAPI ConsoleControlHandler(DWORD control_type);
  static DWORD WINAPI ServiceControlHandler(DWORD control, DWORD event_type,
                                            LPVOID event_data, LPVOID context);

 private:
  SERVICE_STATUS_HANDLE status_handle_ = nullptr;
  SERVICE_STATUS service_status_{};
  HANDLE stop_event_ = nullptr;
  std::thread ipc_thread_;
  std::mutex state_mutex_;
  DWORD active_session_id_ = 0xFFFFFFFF;
  DWORD process_session_id_ = 0xFFFFFFFF;
  DWORD input_desktop_error_code_ = 0;
  DWORD session_helper_process_id_ = 0;
  DWORD session_helper_session_id_ = 0xFFFFFFFF;
  DWORD session_helper_exit_code_ = 0;
  DWORD session_helper_last_error_code_ = 0;
  DWORD session_helper_status_error_code_ = 0;
  DWORD session_helper_report_session_id_ = 0xFFFFFFFF;
  DWORD session_helper_report_process_id_ = 0;
  DWORD session_helper_report_input_desktop_error_code_ = 0;
  DWORD secure_input_helper_process_id_ = 0;
  DWORD secure_input_helper_session_id_ = 0xFFFFFFFF;
  DWORD secure_input_helper_exit_code_ = 0;
  DWORD secure_input_helper_last_error_code_ = 0;
  DWORD last_session_event_type_ = 0;
  DWORD last_session_event_session_id_ = 0xFFFFFFFF;
  ULONGLONG started_at_tick_ = 0;
  ULONGLONG last_sas_tick_ = 0;
  ULONGLONG session_helper_started_at_tick_ = 0;
  ULONGLONG session_helper_report_state_age_ms_ = 0;
  ULONGLONG session_helper_report_uptime_ms_ = 0;
  ULONGLONG secure_input_helper_started_at_tick_ = 0;
  bool session_locked_ = false;
  bool logon_ui_visible_ = false;
  bool prelogin_ = false;
  bool secure_desktop_active_ = false;
  bool input_desktop_available_ = false;
  bool session_helper_running_ = false;
  bool session_helper_status_ok_ = false;
  bool session_helper_report_session_locked_ = false;
  bool session_helper_report_input_desktop_available_ = false;
  bool session_helper_report_lock_app_visible_ = false;
  bool session_helper_report_logon_ui_visible_ = false;
  bool session_helper_report_secure_desktop_active_ = false;
  bool session_helper_report_credential_ui_visible_ = false;
  bool session_helper_report_unlock_ui_visible_ = false;
  bool secure_input_helper_running_ = false;
  bool console_mode_ = false;
  DWORD last_sas_error_code_ = 0;
  bool last_sas_success_ = false;
  HANDLE session_helper_process_handle_ = nullptr;
  HANDLE session_helper_stop_event_ = nullptr;
  HANDLE secure_input_helper_process_handle_ = nullptr;
  HANDLE secure_input_helper_stop_event_ = nullptr;
  std::string input_desktop_name_;
  std::string last_sas_error_;
  std::string session_helper_last_error_;
  std::string session_helper_status_error_;
  std::string session_helper_report_input_desktop_;
  std::string session_helper_report_interactive_stage_;
  std::string secure_input_helper_last_error_;

  static CrossDeskServiceHost* instance_;
};

bool InstallCrossDeskService(const std::wstring& binary_path);
bool UninstallCrossDeskService();
bool StartCrossDeskService();
bool StopCrossDeskService(DWORD timeout_ms = 5000);
std::string QueryCrossDeskService(const std::string& command,
                                  DWORD timeout_ms = 1000);
std::string SendCrossDeskSecureDesktopKeyInput(int key_code, bool is_down,
                                               DWORD timeout_ms = 1000);
std::string SendCrossDeskSecureDesktopMouseInput(int x, int y, int wheel,
                                                 int flag,
                                                 DWORD timeout_ms = 1000);

}  // namespace crossdesk

#endif