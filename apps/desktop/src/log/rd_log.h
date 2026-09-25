/*
 * @Author: DI JUNKUN
 * @Date: 2025-07-21
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RD_LOG_H_
#define _RD_LOG_H_

#include <memory>
#include <string>

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include "spdlog/spdlog.h"

namespace crossdesk {

constexpr auto LOGGER_NAME = "crossdesk";

void InitLogger(const std::string& log_dir,
                const std::string& logger_name = LOGGER_NAME);

std::shared_ptr<spdlog::logger> get_logger();

#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(get_logger(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(get_logger(), __VA_ARGS__)
#define LOG_INFO(...) SPDLOG_LOGGER_INFO(get_logger(), __VA_ARGS__)
#define LOG_WARN(...) SPDLOG_LOGGER_WARN(get_logger(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(get_logger(), __VA_ARGS__)
#define LOG_FATAL(...) SPDLOG_LOGGER_CRITICAL(get_logger(), __VA_ARGS__)
}  // namespace crossdesk
#endif
