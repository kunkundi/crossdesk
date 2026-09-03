/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _REMOTE_ACTION_CODEC_H_
#define _REMOTE_ACTION_CODEC_H_

#include <cstddef>
#include <vector>

#include <remote_action.h>

namespace crossdesk::remote_action_codec {

std::vector<char> Serialize(const RemoteAction &action);
bool Deserialize(const char *data, size_t size, RemoteAction &out);
void Free(RemoteAction &action);

} // namespace crossdesk::remote_action_codec

#endif