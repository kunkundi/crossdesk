/*
 * @Author: DI JUNKUN
 * @Date: 2025-07-21
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RD_LOG_H_
#define _RD_LOG_H_

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "spdlog/common.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO

namespace crossdesk {

constexpr auto LOGGER_NAME = "crossdesk";

void InitLogger(const std::string& log_dir);

std::shared_ptr<spdlog::logger> get_logger();

#define LOG_INFO(...) SPDLOG_LOGGER_INFO(get_logger(), __VA_ARGS__)
#define LOG_WARN(...) SPDLOG_LOGGER_WARN(get_logger(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(get_logger(), __VA_ARGS__)
#define LOG_FATAL(...) SPDLOG_LOGGER_CRITICAL(get_logger(), __VA_ARGS__)
}  // namespace crossdesk
#endif