/*
 * @Author: DI JUNKUN
 * @Date: 2024-05-29
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */
#ifndef _LOCALIZATION_H_
#define _LOCALIZATION_H_

#include <string>
#include <vector>
namespace localization {

std::vector<std::string> menu = {u8"菜单:", "Menu"};
std::vector<std::string> local_id = {u8"本机ID:", "Local ID:"};
std::vector<std::string> password = {u8"密码:", "Password:"};
std::vector<std::string> max_password_len = {u8"最大6个字符", "Max 6 chars"};
std::vector<std::string> remote_id = {u8"远端ID:", "Remote ID:"};
std::vector<std::string> connect = {u8"连接", "Connect"};
std::vector<std::string> disconnect = {u8"断开连接", "Disconnect"};
std::vector<std::string> fullscreen = {u8"全屏", "Fullscreen"};
std::vector<std::string> exit_fullscreen = {u8"退出全屏", "Exit fullscreen"};

}  // namespace localization

#endif