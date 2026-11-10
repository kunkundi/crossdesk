#include "rd_log.h"

#include <filesystem>
#include <mutex>
#include <vector>

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#if defined(__OHOS__)
#include <hilog/log.h>
#include "spdlog/sinks/base_sink.h"
#endif

namespace crossdesk {

namespace {

#if defined(__OHOS__)
class HarmonyLogSink final : public spdlog::sinks::base_sink<std::mutex> {
  void sink_it_(const spdlog::details::log_msg& message) override {
    spdlog::memory_buf_t formatted;
    formatter_->format(message, formatted);
    const std::string text(formatted.data(), formatted.size());
    const auto level = message.level >= spdlog::level::err ? LOG_ERROR :
        message.level >= spdlog::level::warn ? LOG_WARN : LOG_INFO;
    OH_LOG_Print(LOG_APP, level, 0, "CrossDeskDesktop", "%{public}s", text.c_str());
  }
  void flush_() override {}
};
#endif

std::string g_log_dir = "logs";
std::string g_logger_name = LOGGER_NAME;
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

void InitLogger(const std::string& log_dir, const std::string& logger_name) {
  const auto directory = NormalizeLogDirectory(log_dir);
  std::lock_guard<std::mutex> lock(g_logger_mutex);
  if (g_logger) {
    if (directory != g_log_dir || logger_name != g_logger_name) {
      g_logger->warn(
          "InitLogger called after logger initialized. Keeping existing "
          "logger");
      SPDLOG_LOGGER_DEBUG(g_logger, "Keeping logger {} in {}", g_logger_name,
                          g_log_dir);
    }
    return;
  }

  g_log_dir = directory;
  g_logger_name = logger_name;
}

std::shared_ptr<spdlog::logger> get_logger() {
  std::call_once(g_logger_once_flag, []() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_log_dir = NormalizeLogDirectory(g_log_dir);

    std::error_code ec;
    std::filesystem::create_directories(g_log_dir, ec);

    // Reuse the same rotation set across restarts. Timestamped base names
    // give each launch its own backups and let the directory grow forever.
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        g_log_dir + "/" + g_logger_name + ".log", 5 * 1024 * 1024, 3);

    std::vector<spdlog::sink_ptr> sinks;
#if defined(__OHOS__)
    sinks.push_back(std::make_shared<HarmonyLogSink>());
#else
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif
    sinks.push_back(file_sink);

    auto logger = std::make_shared<spdlog::logger>(g_logger_name, sinks.begin(),
                                                   sinks.end());
    logger->set_level(SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
                          ? spdlog::level::debug
                          : spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::register_logger(logger);
    logger->info("Logger initialized");
    SPDLOG_LOGGER_DEBUG(logger, "Log file: {}", file_sink->filename());
    g_logger = std::move(logger);
  });

  return g_logger;
}
}  // namespace crossdesk
