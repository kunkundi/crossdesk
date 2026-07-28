#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path FindRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "xmake.lua") &&
        std::filesystem::exists(
            current / "submodules/minirtc/src/transport/ice_transport_controller.cpp")) {
      return current;
    }
    current = current.parent_path();
  }
  return {};
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string ExtractSection(const std::string& source, const std::string& begin,
                           const std::string& end) {
  const size_t begin_pos = source.find(begin);
  if (begin_pos == std::string::npos) {
    return {};
  }
  const size_t end_pos = source.find(end, begin_pos + begin.size());
  if (end_pos == std::string::npos) {
    return {};
  }
  return source.substr(begin_pos, end_pos - begin_pos);
}

bool ExpectContains(const char* name, const std::string& value,
                    const std::string& expected) {
  if (value.find(expected) != std::string::npos) {
    return true;
  }

  std::cerr << name << " missing expected text: " << expected << "\n";
  return false;
}

bool ExpectNotContains(const char* name, const std::string& value,
                       const std::string& unexpected) {
  if (value.find(unexpected) == std::string::npos) {
    return true;
  }

  std::cerr << name << " contains unsafe text: " << unexpected << "\n";
  return false;
}

}  // namespace

int main() {
  const std::filesystem::path repo_root = FindRepoRoot();
  if (repo_root.empty()) {
    std::cerr << "failed to locate repository root\n";
    return 1;
  }

  const std::string controller = ReadFile(
      repo_root /
      "submodules/minirtc/src/transport/ice_transport_controller.cpp");
  const std::string destroy = ExtractSection(
      controller, "void IceTransportController::Destroy()",
      "uint32_t IceTransportController::AddVideoSendChannel");
  const std::string send_video = ExtractSection(
      controller, "int IceTransportController::SendVideo(",
      "void IceTransportController::MaybeDegradeResolutionOnEncodeTime");
  const std::string on_video_encoded = ExtractSection(
      controller, "int IceTransportController::OnVideoEncoded(",
      "int IceTransportController::SendAudio(");

  bool ok = true;
  ok &= ExpectContains("Destroy", destroy,
                       "std::unique_lock lock(stream_senders_mutex_);");
  ok &= ExpectContains("Destroy", destroy,
                       "senders.swap(stream_senders_);");
  ok &= ExpectContains("Destroy", destroy,
                       "codecs.push_back(std::move(context->codec));");
  ok &= ExpectContains("Destroy", destroy, "senders.clear();");
  ok &= ExpectContains("Destroy", destroy, "codecs.clear();");
  ok &= ExpectNotContains("Destroy", destroy,
                          "std::shared_lock lock(stream_senders_mutex_);");

  ok &= ExpectContains("SendVideo", send_video,
                       "std::weak_ptr<IceTransportController> weak_self");
  ok &= ExpectContains("SendVideo", send_video,
                       "std::weak_ptr<StreamContext> weak_context");
  ok &= ExpectContains(
      "SendVideo", send_video,
      "[weak_self, weak_context, channel_name, queue_delay_ms");
  ok &= ExpectContains("SendVideo", send_video,
                       "return self->OnVideoEncoded(");
  ok &= ExpectNotContains("SendVideo", send_video,
                          "[this, channel_name, context");

  ok &= ExpectContains("OnVideoEncoded", on_video_encoded,
                       "if (!is_running_.load())");
  ok &= ExpectContains("OnVideoEncoded", on_video_encoded,
                       "std::shared_lock lock(stream_senders_mutex_);");
  ok &= ExpectContains("OnVideoEncoded", on_video_encoded,
                       "it->second != context");

  return ok ? 0 : 1;
}
