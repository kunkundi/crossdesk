#include "controller_diagnostics.h"
#include <log.h>
#include <string_view>

namespace crossdesk {
namespace {
// Signaling errors and SDP can contain credentials. Only audited transport
// events reach this sink; arbitrary MiniRTC messages remain suppressed.
std::string DiagnosticEvent(const spdlog::details::log_msg &message) {
  std::string_view source = message.source.filename ? message.source.filename : "";
  const auto slash = source.find_last_of("/\\");
  if (slash != std::string_view::npos)
    source.remove_prefix(slash + 1);
  const std::string_view text(message.payload.data(), message.payload.size());
  if (source == "controller_session.cpp")
    return std::string(text); // Local states/counters only at these call sites.
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

class DiagnosticSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  explicit DiagnosticSink(const std::string &directory)
      : file_(directory + "/transport.log", 1024 * 1024, 2) {}

private:
  void sink_it_(const spdlog::details::log_msg &message) override {
    const auto event = DiagnosticEvent(message);
    if (event.empty())
      return;
    auto sanitized = message;
    sanitized.payload = spdlog::string_view_t(event.data(), event.size());
    file_.log(sanitized);
    file_.flush();
  }
  void flush_() override { file_.flush(); }
  spdlog::sinks::rotating_file_sink_mt file_;
};
} // namespace

void InitializeControllerDiagnostics(const std::string &directory) {
  auto sink = std::make_shared<DiagnosticSink>(directory);
  minirtc::InitLogger(directory);
  auto logger = minirtc::get_logger();
  // Connect calls this after the previous peer and its callbacks stop.
  logger->sinks() = {std::move(sink)};
  logger->set_level(spdlog::level::info);
}
} // namespace crossdesk
