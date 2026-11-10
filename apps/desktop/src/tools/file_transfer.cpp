#include "file_transfer.h"

#include <cstdlib>
#include "rd_log.h"
#if defined(__OHOS__)
#include "platform/path_backend.h"
#endif

namespace crossdesk {

FileReceiver::FileReceiver() : FileReceiver(GetDefaultDesktopPath()) {}

FileReceiver::FileReceiver(const std::filesystem::path& output_dir)
    : output_dir_(output_dir), session_(
          [this](const char*, const char* data, size_t size) {
            FileTransferAck ack{};
            return on_send_ack_ && DecodeFileTransferAck(data, size, &ack)
                ? on_send_ack_(ack) : 0;
          }, {}, {}, [this](const file_transfer::ReceiveState& progress) {
            if (on_receive_progress_) on_receive_progress_(progress);
          }) {
  session_.SetDirectory(output_dir);
}

std::filesystem::path FileReceiver::GetDefaultDesktopPath() {
#if defined(__OHOS__)
  // Harmony has no process HOME/Desktop. Use writable application storage
  // until the user selects and authorizes a public directory in settings.
  const auto desktop_path = platform::GetCachePath("CrossDesk") / "Received Files";
#else
#ifdef _WIN32
  const char* home_env = std::getenv("USERPROFILE");
#else
  const char* home_env = std::getenv("HOME");
#endif
  if (!home_env) {
    return std::filesystem::path{};
  }

  std::filesystem::path desktop_path =
      std::filesystem::path(home_env) / "Desktop";
#endif

  std::error_code ec;
  std::filesystem::create_directories(desktop_path, ec);
  if (ec) {
    LOG_ERROR("FileReceiver: failed to create desktop directory: {}",
              ec.message().c_str());
  }

  return desktop_path;
}

}  // namespace crossdesk
