#ifndef CROSSDESK_LINUX_AUTOSTART_H_
#define CROSSDESK_LINUX_AUTOSTART_H_

#include <string>

namespace crossdesk::platform {

// Repair the old deb entry at normal startup without enabling missing entries
// or overwriting desktop-managed settings and custom commands.
bool MigrateLegacyAutostart(const std::string& app_name);

}  // namespace crossdesk::platform

#endif
