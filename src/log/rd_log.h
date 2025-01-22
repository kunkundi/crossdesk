/*
 * @Author: DI JUNKUN
 * @Date: 2024-11-26
 * Copyright (c) 2024 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _RD_LOG_H_
#define _RD_LOG_H_

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "spdlog/common.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

using namespace std::chrono;

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO

constexpr auto RD_LOGGER_NAME = "rd";

std::shared_ptr<spdlog::logger> get_rd_logger();

#define LOG_INFO(...) SPDLOG_LOGGER_INFO(get_rd_logger(), __VA_ARGS__);

#define LOG_WARN(...) SPDLOG_LOGGER_WARN(get_rd_logger(), __VA_ARGS__);

#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(get_rd_logger(), __VA_ARGS__);

#define LOG_FATAL(...) SPDLOG_LOGGER_CRITICAL(get_rd_logger(), __VA_ARGS__);

#endif