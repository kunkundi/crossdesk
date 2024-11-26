#include "rd_log.h"

std::shared_ptr<spdlog::logger> get_logger() {
  if (auto logger = spdlog::get(LOGGER_NAME)) {
    return logger;
  }

  auto now = std::chrono::system_clock::now() + std::chrono::hours(8);
  auto timet = std::chrono::system_clock::to_time_t(now);
  auto localTime = *std::gmtime(&timet);
  std::stringstream ss;
  std::string filename;
  ss << LOGGER_NAME;
  ss << std::put_time(&localTime, "-%Y%m%d-%H%M%S.log");
  ss >> filename;

  std::string path = "logs/" + filename;
  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      path, 1048576 * 5, 3));

  auto combined_logger =
      std::make_shared<spdlog::logger>(LOGGER_NAME, begin(sinks), end(sinks));
  combined_logger->flush_on(spdlog::level::info);
  spdlog::register_logger(combined_logger);

  return combined_logger;
}
