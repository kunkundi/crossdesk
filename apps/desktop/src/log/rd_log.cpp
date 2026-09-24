#include "rd_log.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace crossdesk {

namespace {

std::string g_log_dir = "logs";
std::once_flag g_logger_once_flag;
std::mutex g_logger_mutex;
std::shared_ptr<spdlog::logger> g_logger;

std::string NormalizeLogDirectory(const std::string& log_dir) {
  const std::filesystem::path path = log_dir.empty() ? "logs" : log_dir;
  std::error_code ec;
  const auto absolute = std::filesystem::absolute(path, ec);
  return (ec ? path : absolute).lexically_normal().string();
}

}  // namespace

void InitLogger(const std::string& log_dir) {
  const auto directory = NormalizeLogDirectory(log_dir);
  std::lock_guard<std::mutex> lock(g_logger_mutex);
  if (g_logger) {
    if (directory != g_log_dir) {
      g_logger->warn(
          "InitLogger called after logger initialized. Ignoring log_dir: {}, "
          "using previous log_dir: {}",
          directory, g_log_dir);
    }
    return;
  }

  g_log_dir = directory;
}

std::shared_ptr<spdlog::logger> get_logger() {
  std::call_once(g_logger_once_flag, []() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_log_dir = NormalizeLogDirectory(g_log_dir);

    std::error_code ec;
    std::filesystem::create_directories(g_log_dir, ec);

    auto now = std::chrono::system_clock::now() + std::chrono::hours(8);
    auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_info;
#ifdef _WIN32
    gmtime_s(&tm_info, &now_time);
#else
    gmtime_r(&now_time, &tm_info);
#endif

    std::stringstream ss;
    ss << LOGGER_NAME;
    ss << std::put_time(&tm_info, "-%Y%m%d-%H%M%S.log");

    std::string filename = g_log_dir + "/" + ss.str();

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        filename, 5 * 1024 * 1024, 3));

    auto logger = std::make_shared<spdlog::logger>(LOGGER_NAME, sinks.begin(),
                                                   sinks.end());
    logger->flush_on(spdlog::level::info);
    spdlog::register_logger(logger);
    logger->info("Logger initialized: path={}", filename);
    g_logger = std::move(logger);
  });

  return g_logger;
}
}  // namespace crossdesk
