/*
 * Shared Wayland portal session state used by the Linux Wayland capturer and
 * mouse controller so they can reuse one RemoteDesktop session.
 */

#ifndef _WAYLAND_PORTAL_SHARED_H_
#define _WAYLAND_PORTAL_SHARED_H_

#include <cstdint>
#include <string>

struct DBusConnection;

namespace crossdesk {

struct SharedWaylandPortalSessionInfo {
  DBusConnection* connection = nullptr;
  std::string session_handle;
  uint32_t stream_id = 0;
  int width = 0;
  int height = 0;
  bool pointer_granted = false;
};

bool PublishSharedWaylandPortalSession(
    const SharedWaylandPortalSessionInfo& info);
bool AcquireSharedWaylandPortalSession(bool require_pointer,
                                       SharedWaylandPortalSessionInfo* out);
bool ReleaseSharedWaylandPortalSession(DBusConnection** connection_out,
                                       std::string* session_handle_out);
void CloseWaylandPortalSessionAndConnection(DBusConnection* connection,
                                            const std::string& session_handle,
                                            const char* close_action);

}  // namespace crossdesk

#endif
