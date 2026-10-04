#ifndef CROSSDESK_WINDOWS_AUTOSTART_TASK_H_
#define CROSSDESK_WINDOWS_AUTOSTART_TASK_H_

#include <string>

namespace crossdesk::platform {

// Keep this in sync with crossdesk.rc and crossdesk_portable.rc. Elevating a
// portable build manually must not change its normal login startup policy.
#if !defined(CROSSDESK_DEBUG) && \
    (!defined(CROSSDESK_PORTABLE) || !CROSSDESK_PORTABLE)
inline constexpr bool kAutostartRequiresElevation = true;
#else
inline constexpr bool kAutostartRequiresElevation = false;
#endif

inline std::wstring AutostartTaskName(const std::wstring& app_name,
                                      const std::wstring& user_sid) {
  return app_name + L" Autostart " + user_sid;
}

inline std::wstring EscapeTaskXml(const std::wstring& value) {
  std::wstring escaped;
  for (const wchar_t character : value) {
    switch (character) {
      case L'&':
        escaped += L"&amp;";
        break;
      case L'<':
        escaped += L"&lt;";
        break;
      case L'>':
        escaped += L"&gt;";
        break;
      case L'"':
        escaped += L"&quot;";
        break;
      case L'\'':
        escaped += L"&apos;";
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

inline std::wstring BuildAutostartTaskXml(const std::wstring& user_sid,
                                          const std::wstring& executable,
                                          const std::wstring& directory) {
  const auto sid = EscapeTaskXml(user_sid);
  // InteractiveToken keeps the GUI in this user's logged-in desktop and does
  // not store a password. A remote desktop client must also survive battery
  // operation and the scheduler's default three-day execution time limit.
  return L"<Task version=\"1.3\" "
         L"xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
         L"<Triggers><LogonTrigger><Enabled>true</Enabled>"
         L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>" +
         sid +
         L"</UserId></LogonTrigger></Triggers>"
         L"<Principals><Principal id=\"CurrentUser\"><UserId>" +
         sid +
         L"</UserId><LogonType>InteractiveToken</LogonType>"
         L"<RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
         L"<Settings><MultipleInstancesPolicy>IgnoreNew</"
         L"MultipleInstancesPolicy>"
         L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
         L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
         L"<AllowHardTerminate>false</AllowHardTerminate>"
         L"<Enabled>true</Enabled><ExecutionTimeLimit>PT0S</ExecutionTimeLimit>"
         L"</Settings><Actions Context=\"CurrentUser\"><Exec><Command>" +
         EscapeTaskXml(executable) + L"</Command><WorkingDirectory>" +
         EscapeTaskXml(directory) +
         L"</WorkingDirectory></Exec></Actions></Task>";
}

// Called at normal startup and by the installer after an upgrade. Only migrate
// a legacy Run entry pointing to this executable, leaving other copies alone.
bool MigrateLegacyAutostart(const std::string& app_name);

// Installer cleanup: remove tasks for this executable across users, preserving
// registrations belonging to other installation/portable directories.
bool RemoveInstalledAutostart(const std::string& app_name);

}  // namespace crossdesk::platform

#endif
