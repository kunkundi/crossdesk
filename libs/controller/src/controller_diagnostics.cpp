#include "controller_diagnostics.h"
#include <minirtc.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace crossdesk {
namespace {
// Signaling errors and SDP can contain credentials. Only audited transport
// events reach this sink; arbitrary MiniRTC messages remain suppressed.
std::string DiagnosticEvent(const MiniRtcLogRecord &message) {
  std::string_view source = message.source_file ? message.source_file : "";
  const auto slash = source.find_last_of("/\\");
  if (slash != std::string_view::npos)
    source.remove_prefix(slash + 1);
  const std::string_view text(message.message, message.message_size);
  if (source == "ws_client.cpp") {
    if (text == "WebSocket connection opened") return std::string(text);
    // These call sites contain library category names, numeric codes and retry
    // counters. Keep free-form errors, server close reasons and URLs filtered.
    for (const std::string_view prefix : {
             "Signaling transport failure: category=",
             "Signaling transport closed: code=",
             "Scheduling signaling reconnect: attempt=",
             "Will retry connection after "})
      if (text.substr(0, prefix.size()) == prefix) return std::string(text);
  }
  if (source == "ice_transport.cpp") {
    const auto marker = text.rfind("state_change: ");
    if (marker != std::string_view::npos) {
      const auto state = text.substr(marker + 14);
      for (const auto allowed : {"disconnected", "gathering", "connecting",
                                 "connected", "ready", "failed"})
        if (state == allowed)
          return "ICE state: " + std::string(state);
    }
    for (const auto allowed : {"Traversal using relay server", "Traversal using p2p",
                               "Selected ICE pair is not available yet"})
      if (text == allowed)
        return std::string(text);
  }
  if (source == "ice_agent.cpp") {
    for (const auto allowed : {"DTLS handshake started, awaiting peer packets...",
                               "DTLS handshake retransmission failed",
                               "DTLS peer certificate missing",
                               "DTLS peer fingerprint mismatch",
                               "DTLS peer fingerprint verified",
                               "DTLS peer not verified", "No SRTP profile selected",
                               "SSL_export_keying_material failed", "StartDtls failed",
                               "libnice has no relay upgrade extension; rebuild dependencies"})
      if (text == allowed)
        return std::string(text);
    for (const auto prefix : {"DTLS handshake timed out after ",
                              "DTLS handshake start failed, err=",
                              "DTLS packet processing failed, err="})
      if (text.substr(0, std::char_traits<char>::length(prefix)) == prefix)
        return std::string(prefix); // Omit variable payload entirely.
  }
  return {};
}

class DiagnosticWriter {
public:
  explicit DiagnosticWriter(const std::string &directory)
      : file_(directory + "/transport.log", 1024 * 1024, 2) {}
  void Write(const std::string &event, const MiniRtcLogRecord *record = nullptr) {
    if (event.empty()) return;
    spdlog::details::log_msg message("crossdesk", spdlog::level::info, event);
    if (record) {
      message.level = static_cast<spdlog::level::level_enum>(record->level);
      message.time = spdlog::log_clock::time_point(std::chrono::microseconds(record->timestamp_us));
      message.source = {record->source_file, record->source_line, ""};
    }
    file_.log(message);
    file_.flush();
  }
private:
  spdlog::sinks::rotating_file_sink_mt file_;
};
void ReceiveLog(const MiniRtcLogRecord *message, void *context) {
  if (message) static_cast<DiagnosticWriter *>(context)->Write(DiagnosticEvent(*message), message);
}
struct Diagnostics {
  std::mutex initialization, mutex;
  std::shared_ptr<DiagnosticWriter> writer;
  ~Diagnostics() {
    // Quiesce the callback before releasing its application-owned context.
    MiniRtcConfigureLogging(MiniRtcLogOff, nullptr, nullptr);
  }
};
Diagnostics &State() {
  static Diagnostics state;
  return state;
}
} // namespace

void InitializeControllerDiagnostics(const std::string &directory) {
  auto &state = State();
  std::lock_guard initialize(state.initialization);
  auto writer = std::make_shared<DiagnosticWriter>(directory);
  if (MiniRtcConfigureLogging(MiniRtcLogInfo, ReceiveLog, writer.get()) != 0)
    throw std::runtime_error("Unable to configure MiniRTC diagnostics");
  std::lock_guard lock(state.mutex);
  state.writer = std::move(writer);
}
void WriteControllerDiagnostic(const std::string &message) noexcept {
  try {
    auto &state = State();
    std::shared_ptr<DiagnosticWriter> writer;
    { std::lock_guard lock(state.mutex); writer = state.writer; }
    if (writer) writer->Write(message);
  } catch (...) {}
}
} // namespace crossdesk
