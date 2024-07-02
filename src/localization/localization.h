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

static std::vector<std::string> local_desktop = {u8"本桌面", "Local Desktop"};
static std::vector<std::string> local_id = {u8"本机ID", "Local ID"};
static std::vector<std::string> password = {u8"密码", "Password"};
static std::vector<std::string> max_password_len = {u8"最大6个字符",
                                                    "Max 6 chars"};

static std::vector<std::string> remote_desktop = {u8"控制远程桌面",
                                                  "Control Remote Desktop"};
static std::vector<std::string> remote_id = {u8"对端ID", "Remote ID"};
static std::vector<std::string> connect = {u8"连接", "Connect"};
static std::vector<std::string> disconnect = {u8"断开连接", "Disconnect"};
static std::vector<std::string> fullscreen = {u8" 全屏", " Fullscreen"};
static std::vector<std::string> exit_fullscreen = {u8" 退出全屏",
                                                   " Exit fullscreen"};
static std::vector<std::string> control_mouse = {u8" 控制", " Control"};
static std::vector<std::string> release_mouse = {u8" 释放", " Release"};
static std::vector<std::string> settings = {u8" 设置", " Settings"};
static std::vector<std::string> language = {u8"语言:", "Language:"};
static std::vector<std::string> language_zh = {u8"中文", "Chinese"};
static std::vector<std::string> language_en = {u8"英文", "English"};
static std::vector<std::string> video_quality = {u8"视频质量:",
                                                 "Video Quality:"};
static std::vector<std::string> video_quality_high = {u8"高", "High"};
static std::vector<std::string> video_quality_medium = {u8"中", "Medium"};
static std::vector<std::string> video_quality_low = {u8"低", "Low"};
static std::vector<std::string> video_encode_format = {u8"视频编码格式:",
                                                       "Video Encode Format:"};
static std::vector<std::string> av1 = {u8"AV1", "AV1"};
static std::vector<std::string> h264 = {u8"H.264", "H.264"};
static std::vector<std::string> enable_hardware_video_codec = {
    u8"启用硬件编解码器:", "Enable Hardware Video Codec:"};

static std::vector<std::string> ok = {u8"确认", "OK"};
static std::vector<std::string> cancel = {u8"取消", "Cancel"};

static std::vector<std::string> signal_connected = {u8"已连接服务器",
                                                    "Connected"};
static std::vector<std::string> signal_disconnected = {u8"未连接服务器",
                                                       "Disconnected"};

static std::vector<std::string> p2p_connected = {u8"对等连接已建立",
                                                 "P2P Connected"};
static std::vector<std::string> p2p_disconnected = {u8"对等连接已断开",
                                                    "P2P Disconnected"};
static std::vector<std::string> p2p_connecting = {u8"正在建立对等连接",
                                                  "P2P Connecting"};

}  // namespace localization

#endif