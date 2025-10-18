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

static std::vector<std::string> local_desktop = {
    reinterpret_cast<const char*>(u8"本桌面"), "Local Desktop"};
static std::vector<std::string> local_id = {
    reinterpret_cast<const char*>(u8"本机ID"), "Local ID"};
static std::vector<std::string> local_id_copied_to_clipboard = {
    reinterpret_cast<const char*>(u8"已复制到剪贴板"), "Copied to clipboard"};
static std::vector<std::string> password = {
    reinterpret_cast<const char*>(u8"密码"), "Password"};
static std::vector<std::string> max_password_len = {
    reinterpret_cast<const char*>(u8"最大6个字符"), "Max 6 chars"};

static std::vector<std::string> remote_desktop = {
    reinterpret_cast<const char*>(u8"控制远程桌面"), "Control Remote Desktop"};
static std::vector<std::string> remote_id = {
    reinterpret_cast<const char*>(u8"对端ID"), "Remote ID"};
static std::vector<std::string> connect = {
    reinterpret_cast<const char*>(u8"连接"), "Connect"};
static std::vector<std::string> recent_connections = {
    reinterpret_cast<const char*>(u8"近期连接"), "Recent Connections"};
static std::vector<std::string> disconnect = {
    reinterpret_cast<const char*>(u8"断开连接"), "Disconnect"};
static std::vector<std::string> fullscreen = {
    reinterpret_cast<const char*>(u8"全屏"), " Fullscreen"};
static std::vector<std::string> show_net_traffic_stats = {
    reinterpret_cast<const char*>(u8"显示流量统计"), "Show Net Traffic Stats"};
static std::vector<std::string> hide_net_traffic_stats = {
    reinterpret_cast<const char*>(u8"隐藏流量统计"), "Hide Net Traffic Stats"};
static std::vector<std::string> video = {
    reinterpret_cast<const char*>(u8"视频"), "Video"};
static std::vector<std::string> audio = {
    reinterpret_cast<const char*>(u8"音频"), "Audio"};
static std::vector<std::string> data = {reinterpret_cast<const char*>(u8"数据"),
                                        "Data"};
static std::vector<std::string> total = {
    reinterpret_cast<const char*>(u8"总计"), "Total"};
static std::vector<std::string> in = {reinterpret_cast<const char*>(u8"输入"),
                                      "In"};
static std::vector<std::string> out = {reinterpret_cast<const char*>(u8"输出"),
                                       "Out"};
static std::vector<std::string> loss_rate = {
    reinterpret_cast<const char*>(u8"丢包率"), "Loss Rate"};
static std::vector<std::string> exit_fullscreen = {
    reinterpret_cast<const char*>(u8"退出全屏"), "Exit fullscreen"};
static std::vector<std::string> control_mouse = {
    reinterpret_cast<const char*>(u8"控制"), "Control"};
static std::vector<std::string> release_mouse = {
    reinterpret_cast<const char*>(u8"释放"), "Release"};
static std::vector<std::string> audio_capture = {
    reinterpret_cast<const char*>(u8"声音"), "Audio"};
static std::vector<std::string> mute = {
    reinterpret_cast<const char*>(u8" 静音"), " Mute"};
static std::vector<std::string> settings = {
    reinterpret_cast<const char*>(u8"设置"), "Settings"};
static std::vector<std::string> language = {
    reinterpret_cast<const char*>(u8"语言:"), "Language:"};
static std::vector<std::string> language_zh = {
    reinterpret_cast<const char*>(u8"中文"), "Chinese"};
static std::vector<std::string> language_en = {
    reinterpret_cast<const char*>(u8"英文"), "English"};
static std::vector<std::string> video_quality = {
    reinterpret_cast<const char*>(u8"视频质量:"), "Video Quality:"};
static std::vector<std::string> video_frame_rate = {
    reinterpret_cast<const char*>(u8"画面采集帧率:"),
    "Video Capture Frame Rate:"};
static std::vector<std::string> video_quality_high = {
    reinterpret_cast<const char*>(u8"高"), "High"};
static std::vector<std::string> video_quality_medium = {
    reinterpret_cast<const char*>(u8"中"), "Medium"};
static std::vector<std::string> video_quality_low = {
    reinterpret_cast<const char*>(u8"低"), "Low"};
static std::vector<std::string> video_encode_format = {
    reinterpret_cast<const char*>(u8"视频编码格式:"), "Video Encode Format:"};
static std::vector<std::string> av1 = {reinterpret_cast<const char*>(u8"AV1"),
                                       "AV1"};
static std::vector<std::string> h264 = {
    reinterpret_cast<const char*>(u8"H.264"), "H.264"};
static std::vector<std::string> enable_hardware_video_codec = {
    reinterpret_cast<const char*>(u8"启用硬件编解码器:"),
    "Enable Hardware Video Codec:"};
static std::vector<std::string> enable_turn = {
    reinterpret_cast<const char*>(u8"启用中继服务:"), "Enable TURN Service:"};
static std::vector<std::string> enable_srtp = {
    reinterpret_cast<const char*>(u8"启用SRTP:"), "Enable SRTP:"};
static std::vector<std::string> self_hosted_server_config = {
    reinterpret_cast<const char*>(u8"自托管服务器配置"),
    "Self-Hosted Server Config"};
static std::vector<std::string> self_hosted_server_settings = {
    reinterpret_cast<const char*>(u8"自托管服务器设置"),
    "Self-Hosted Server Settings"};
static std::vector<std::string> self_hosted_server_address = {
    reinterpret_cast<const char*>(u8"服务器地址:"), "Server Address:"};
static std::vector<std::string> self_hosted_server_port = {
    reinterpret_cast<const char*>(u8"服务器端口:"), "Server Port:"};
static std::vector<std::string> self_hosted_server_certificate_path = {
    reinterpret_cast<const char*>(u8"证书文件路径:"), "Certificate File Path:"};
static std::vector<std::string> select_a_file = {
    reinterpret_cast<const char*>(u8"请选择文件"), "Please select a file"};
static std::vector<std::string> ok = {reinterpret_cast<const char*>(u8"确认"),
                                      "OK"};
static std::vector<std::string> cancel = {
    reinterpret_cast<const char*>(u8"取消"), "Cancel"};

static std::vector<std::string> new_password = {
    reinterpret_cast<const char*>(u8"请输入六位密码:"),
    "Please input a six-char password:"};

static std::vector<std::string> input_password = {
    reinterpret_cast<const char*>(u8"请输入密码:"), "Please input password:"};
static std::vector<std::string> validate_password = {
    reinterpret_cast<const char*>(u8"验证密码中..."), "Validate password ..."};
static std::vector<std::string> reinput_password = {
    reinterpret_cast<const char*>(u8"请重新输入密码"),
    "Please input password again"};

static std::vector<std::string> remember_password = {
    reinterpret_cast<const char*>(u8"记住密码"), "Remember password"};

static std::vector<std::string> signal_connected = {
    reinterpret_cast<const char*>(u8"已连接服务器"), "Connected"};
static std::vector<std::string> signal_disconnected = {
    reinterpret_cast<const char*>(u8"未连接服务器"), "Disconnected"};

static std::vector<std::string> p2p_connected = {
    reinterpret_cast<const char*>(u8"对等连接已建立"), "P2P Connected"};
static std::vector<std::string> p2p_disconnected = {
    reinterpret_cast<const char*>(u8"对等连接已断开"), "P2P Disconnected"};
static std::vector<std::string> p2p_connecting = {
    reinterpret_cast<const char*>(u8"正在建立对等连接..."),
    "P2P Connecting ..."};
static std::vector<std::string> p2p_failed = {
    reinterpret_cast<const char*>(u8"对等连接失败"), "P2P Failed"};
static std::vector<std::string> p2p_closed = {
    reinterpret_cast<const char*>(u8"对等连接已关闭"), "P2P closed"};

static std::vector<std::string> no_such_id = {
    reinterpret_cast<const char*>(u8"无此ID"), "No such ID"};

static std::vector<std::string> about = {
    reinterpret_cast<const char*>(u8"关于"), "About"};
static std::vector<std::string> version = {
    reinterpret_cast<const char*>(u8"版本"), "Version"};

static std::vector<std::string> confirm_delete_connection = {
    reinterpret_cast<const char*>(u8"确认删除此连接"),
    "Confirm to delete this connection"};
}  // namespace localization

#endif