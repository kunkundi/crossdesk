#include "desktop_automation.h"

#include <algorithm>
#include <stdexcept>

namespace crossdesk {
using nlohmann::json;
std::string DesktopText(std::string text, size_t limit) {
  if (text.size() > limit) {
    while (limit && (static_cast<unsigned char>(text[limit]) & 0xc0) == 0x80) --limit;
    text.resize(limit);
  }
  for (auto& character : text) if (static_cast<unsigned char>(character) < 32 && character != '\n') character = ' ';
  return json::parse(json(text).dump(-1, ' ', false, json::error_handler_t::replace)).get<std::string>();
}
json DesktopAutomation::Execute(const std::string& owner, const json& request) {
  const auto now = std::chrono::steady_clock::now();
  while (!snapshots_.empty() && snapshots_.front().expires <= now) snapshots_.pop_front();
  const auto operation = request.at("operation").get<std::string>();
  if (operation == "capabilities") return DesktopCapabilities();
  if (operation == "launch" || operation == "open_url") {
    const auto value = request.at(operation == "launch" ? "application" : "url").get<std::string>();
    if (value.empty() || value.size() > 4096 || value.find_first_of("\r\n") != std::string::npos ||
        value.find('\0') != std::string::npos) throw std::invalid_argument("INVALID_ARGUMENT");
    if (operation == "launch") return DesktopLaunch(value);
    if (value.rfind("https://", 0) != 0 && value.rfind("http://", 0) != 0)
      throw std::invalid_argument("INVALID_URL");
    return DesktopOpenUrl(value);
  }
  DesktopItem item;
  auto found = snapshots_.end();
  if (request.contains("snapshot_id")) {
    const auto id = request.at("snapshot_id").get<std::string>();
    found = std::find_if(snapshots_.begin(), snapshots_.end(), [&](const auto& snapshot) {
      return snapshot.id == id && snapshot.owner == owner;
    });
    if (found == snapshots_.end()) throw std::invalid_argument("STALE_UI_SNAPSHOT");
    const int index = request.at("element_id").get<int>();
    if (index < 0 || size_t(index) >= found->content.items.size()) throw std::invalid_argument("INVALID_ELEMENT");
    item = found->content.items[size_t(index)];
  }
  if (operation == "windows" || operation == "tree") {
    const int limit = request.value("max_nodes", 128), depth = request.value("max_depth", 8);
    const int pid = request.value("pid", 0);
    if (limit < 1 || limit > 128 || depth < 0 || depth > 16 || pid < 0)
      throw std::invalid_argument("INVALID_ARGUMENT");
    DesktopSnapshot content;
    if (operation == "windows") content = DesktopWindows();
    else if (found != snapshots_.end()) {
      if (!item.inspect) throw std::invalid_argument("UNSUPPORTED_OPERATION");
      content = item.inspect(limit, depth);
    } else content = DesktopTree(pid, limit, depth);
    json elements = json::array();
    size_t bytes = 0;
    for (size_t index = 0; index < content.items.size(); ++index) {
      auto data = content.items[index].data;
      data["element_id"] = index;
      bytes += data.dump(-1, ' ', false, json::error_handler_t::replace).size();
      if (bytes > 48000 || index >= 128) { content.truncated = true; content.items.resize(index); break; }
      elements.push_back(std::move(data));
    }
    const auto id = "ui_" + std::to_string(++sequence_);
    if (snapshots_.size() >= 8) snapshots_.pop_front();
    const bool truncated = content.truncated;
    snapshots_.push_back({id, owner, now + std::chrono::seconds(30), std::move(content)});
    return {{"snapshot_id", id}, {"expires_in_ms", 30000}, {"elements", elements},
      {"truncated", truncated}, {"coordinate_space", "desktop_native"}};
  }
  if (operation != "activate" && operation != "press" && operation != "focus" && operation != "set_value")
    throw std::invalid_argument("UNSUPPORTED_OPERATION");
  if (found == snapshots_.end() || !item.perform) throw std::invalid_argument("INVALID_ELEMENT");
  const auto value = request.value("value", std::string{});
  if (value.size() > 16384 || value.find('\0') != std::string::npos) throw std::invalid_argument("INVALID_ARGUMENT");
  if (item.data.value("secure", false)) throw std::invalid_argument("SECURE_ELEMENT");
  // The next operation must inspect the resulting UI again, even if an OS
  // provider returns an uncertain result after accepting a mutation.
  snapshots_.erase(found);
  return item.perform(operation, value);
}
#if defined(__OHOS__)
json DesktopCapabilities() { return {{"available", false}}; }
DesktopSnapshot DesktopWindows() { throw std::runtime_error("UI_AUTOMATION_UNSUPPORTED"); }
DesktopSnapshot DesktopTree(int, int, int) { throw std::runtime_error("UI_AUTOMATION_UNSUPPORTED"); }
json DesktopLaunch(const std::string&) { throw std::runtime_error("UI_AUTOMATION_UNSUPPORTED"); }
json DesktopOpenUrl(const std::string&) { throw std::runtime_error("UI_AUTOMATION_UNSUPPORTED"); }
#endif
} // namespace crossdesk
