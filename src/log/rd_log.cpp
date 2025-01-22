#include "rd_log.h"

std::shared_ptr<spdlog::logger> get_rd_logger() {
  if (auto logger = spdlog::get(RD_LOGGER_NAME)) {
    return logger;
  }

  auto now = std::chrono::system_clock::now() + std::chrono::hours(8);
  auto now_time = std::chrono::system_clock::to_time_t(now);

  std::tm tm_info;

#ifdef _WIN32
  gmtime_s(&tm_info, &now_time);
#else
  gmtime_r(&now_time, &tm_info);
#endif

  std::stringstream ss;
  std::string filename;
  ss << RD_LOGGER_NAME;
  ss << std::put_time(&tm_info, "-%Y%m%d-%H%M%S.log");
  ss >> filename;

  std::string path = "logs/" + filename;
  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      path, 1048576 * 5, 3));

  auto combined_logger = std::make_shared<spdlog::logger>(
      RD_LOGGER_NAME, begin(sinks), end(sinks));
  combined_logger->flush_on(spdlog::level::info);
  spdlog::register_logger(combined_logger);

  return combined_logger;
}
