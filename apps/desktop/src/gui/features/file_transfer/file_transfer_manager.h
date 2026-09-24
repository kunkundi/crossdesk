#ifndef CROSSDESK_GUI_FILE_TRANSFER_MANAGER_H_
#define CROSSDESK_GUI_FILE_TRANSFER_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <mutex>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "runtime/remote_session.h"

namespace crossdesk {

class GuiRuntime;

class FileTransferManager {
public:
  using FileTransferState = gui_detail::FileTransferState;
  using RemoteSession = gui_detail::RemoteSession;

  explicit FileTransferManager(GuiRuntime &owner);
  ~FileTransferManager();

  FileTransferState &global_state();
  FileTransferState &
  state_for(const std::shared_ptr<RemoteSession> &props);

  void
  ProcessSelectedFile(const std::string &path,
                      const std::shared_ptr<RemoteSession> &props,
                      const std::string &file_label,
                      const std::string &remote_id = "");
  void HandleAck(const char *data, size_t size);

private:
  void Start(std::shared_ptr<RemoteSession> props,
             const std::filesystem::path &file_path,
             const std::string &file_label, const std::string &remote_id = "");
  void ProcessQueue(std::shared_ptr<RemoteSession> props);
  void Unregister(uint32_t file_id, bool per_peer);

  std::mutex workers_mutex_;
  std::vector<std::future<void>> workers_;
  bool stopping_ = false;
  GuiRuntime &owner_;
  FileTransferState global_state_;
  std::unordered_map<uint32_t, std::weak_ptr<RemoteSession>>
      file_id_to_props_;
  std::shared_mutex file_id_to_props_mutex_;
  std::unordered_map<uint32_t, FileTransferState *> file_id_to_state_;
  std::shared_mutex file_id_to_state_mutex_;
};

} // namespace crossdesk

#endif // CROSSDESK_GUI_FILE_TRANSFER_MANAGER_H_
