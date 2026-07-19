#ifndef CROSSDESK_GUI_REMOTE_ACTION_CODEC_H_
#define CROSSDESK_GUI_REMOTE_ACTION_CODEC_H_

#include <cstddef>
#include <vector>

#include "device_controller.h"

namespace crossdesk::remote_action_codec {

std::vector<char> Serialize(const RemoteAction &action);
bool Deserialize(const char *data, size_t size, RemoteAction &out);
void Free(RemoteAction &action);

} // namespace crossdesk::remote_action_codec

#endif // CROSSDESK_GUI_REMOTE_ACTION_CODEC_H_
