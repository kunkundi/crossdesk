#include "desktop_automation.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <deque>
#include <memory>
#include <stdexcept>

namespace crossdesk {
namespace {
using nlohmann::json;
using Element = std::shared_ptr<const __AXUIElement>;
Element Own(AXUIElementRef element) { return Element(element, [](auto value) { if (value) CFRelease(value); }); }
CFTypeRef Attribute(AXUIElementRef element, CFStringRef name) {
  CFTypeRef value = nullptr;
  AXUIElementCopyAttributeValue(element, name, &value);
  return value;
}
std::string String(CFTypeRef value) {
  if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return {};
  char bytes[2048]{};
  CFIndex used = 0;
  const auto count = CFStringGetBytes(static_cast<CFStringRef>(value),
      CFRangeMake(0, std::min<CFIndex>(CFStringGetLength(static_cast<CFStringRef>(value)), 256)),
      kCFStringEncodingUTF8, 0, false, reinterpret_cast<UInt8*>(bytes), sizeof(bytes) - 1, &used);
  (void)count;
  return DesktopText(std::string(bytes, size_t(used)));
}
std::string Text(AXUIElementRef element, CFStringRef name) {
  const auto value = Attribute(element, name);
  auto result = String(value);
  if (value) CFRelease(value);
  return result;
}
void Accessible() { if (!AXIsProcessTrusted()) throw std::runtime_error("ACCESSIBILITY_PERMISSION_REQUIRED"); }
json Perform(Element element, const std::string& operation, const std::string& value) {
  Accessible();
  if (Text(element.get(), kAXSubroleAttribute) == "AXSecureTextField") throw std::runtime_error("SECURE_ELEMENT");
  AXError error = kAXErrorActionUnsupported;
  if (operation == "press") error = AXUIElementPerformAction(element.get(), kAXPressAction);
  else if (operation == "activate") {
    pid_t pid = 0;
    AXUIElementGetPid(element.get(), &pid);
    [[NSRunningApplication runningApplicationWithProcessIdentifier:pid] activateWithOptions:0];
    error = AXUIElementPerformAction(element.get(), kAXRaiseAction);
  } else if (operation == "focus") error = AXUIElementSetAttributeValue(element.get(), kAXFocusedAttribute, kCFBooleanTrue);
  else if (operation == "set_value") {
    const auto text = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(value.data()), value.size(), kCFStringEncodingUTF8, false);
    if (!text) throw std::invalid_argument("INVALID_ARGUMENT");
    error = AXUIElementSetAttributeValue(element.get(), kAXValueAttribute, text);
    CFRelease(text);
  }
  if (error != kAXErrorSuccess) throw std::runtime_error(error == kAXErrorInvalidUIElement ? "STALE_ELEMENT" : "UI_ACTION_FAILED");
  return {{"accepted", true}};
}
DesktopSnapshot Tree(Element root, int limit, int depth) {
  Accessible();
  struct Entry { Element element; int depth, parent; };
  std::deque<Entry> queue{{root, 0, -1}};
  DesktopSnapshot result;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (!queue.empty() && int(result.items.size()) < limit && std::chrono::steady_clock::now() < deadline) {
    auto entry = std::move(queue.front()); queue.pop_front();
    const auto element = entry.element.get();
    AXUIElementSetMessagingTimeout(element, .1f);
    const auto role = Text(element, kAXRoleAttribute);
    if (role.empty()) continue;
    const bool secure = Text(element, kAXSubroleAttribute) == "AXSecureTextField";
    json data{{"role", role}, {"name", Text(element, kAXTitleAttribute)}, {"parent", entry.parent}, {"secure", secure}};
    if (!secure) data["value"] = Text(element, kAXValueAttribute);
    CGPoint point{}; CGSize size{};
    auto position = Attribute(element, kAXPositionAttribute), extent = Attribute(element, kAXSizeAttribute);
    if (position && extent && CFGetTypeID(position) == AXValueGetTypeID() && CFGetTypeID(extent) == AXValueGetTypeID() &&
        AXValueGetValue(static_cast<AXValueRef>(position), kAXValueTypeCGPoint, &point) &&
        AXValueGetValue(static_cast<AXValueRef>(extent), kAXValueTypeCGSize, &size))
      data["bounds"] = {{"x", point.x}, {"y", point.y}, {"width", size.width}, {"height", size.height}};
    if (position) CFRelease(position);
    if (extent) CFRelease(extent);
    const int index = int(result.items.size());
    result.items.push_back({data,
      [element = entry.element](const auto& operation, const auto& value) { return Perform(element, operation, value); },
      [element = entry.element](int max, int levels) { return Tree(element, max, levels); }});
    CFIndex count = 0;
    if (AXUIElementGetAttributeValueCount(element, kAXChildrenAttribute, &count) != kAXErrorSuccess || count <= 0) continue;
    if (entry.depth >= depth) { result.truncated = true; continue; }
    const auto available = std::max(0, limit - int(result.items.size()) - int(queue.size()));
    if (count > available) result.truncated = true;
    CFArrayRef children = nullptr;
    if (available && AXUIElementCopyAttributeValues(element, kAXChildrenAttribute, 0, std::min<CFIndex>(count, available), &children) == kAXErrorSuccess) {
      for (CFIndex index = 0; index < CFArrayGetCount(children); ++index) {
        const auto child = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(children, index));
        if (CFGetTypeID(child) == AXUIElementGetTypeID()) {
          CFRetain(child); queue.push_back({Own(child), entry.depth + 1, int(result.items.size()) - 1});
        }
      }
      CFRelease(children);
    }
  }
  result.truncated |= !queue.empty();
  return result;
}
} // namespace
nlohmann::json DesktopCapabilities() {
  return {{"available", true}, {"backend", "macos-ax"}, {"accessibility_permission", bool(AXIsProcessTrusted())},
    {"windows", bool(AXIsProcessTrusted())}, {"tree", bool(AXIsProcessTrusted())}, {"launch", true}, {"open_url", true}};
}
DesktopSnapshot DesktopTree(int pid, int limit, int depth) {
  @autoreleasepool {
    if (!pid) pid = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
    if (pid <= 0) throw std::runtime_error("APPLICATION_UNAVAILABLE");
    return Tree(Own(AXUIElementCreateApplication(pid)), limit, depth);
  }
}
DesktopSnapshot DesktopWindows() {
  @autoreleasepool {
    DesktopSnapshot result;
    const auto front = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
    // AX window references provide exact activation targets when permission is present.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
    Accessible();
    for (NSRunningApplication* app in NSWorkspace.sharedWorkspace.runningApplications) {
      if (std::chrono::steady_clock::now() >= deadline) { result.truncated = true; break; }
      if (app.activationPolicy != NSApplicationActivationPolicyRegular) continue;
      auto application = Own(AXUIElementCreateApplication(app.processIdentifier));
      AXUIElementSetMessagingTimeout(application.get(), .1f);
      const auto raw = Attribute(application.get(), kAXWindowsAttribute);
      if (raw && CFGetTypeID(raw) == CFArrayGetTypeID()) {
        const auto windows = static_cast<CFArrayRef>(raw);
        for (CFIndex index = 0; index < CFArrayGetCount(windows) && result.items.size() < 128; ++index) {
          if (std::chrono::steady_clock::now() >= deadline) { result.truncated = true; break; }
          const auto window = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(windows, index));
          if (CFGetTypeID(window) != AXUIElementGetTypeID()) continue;
          CFRetain(window); const auto element = Own(window);
          const auto main = Attribute(window, kAXMainAttribute);
          const bool foreground = app.processIdentifier == front && main == kCFBooleanTrue;
          if (main) CFRelease(main);
          result.items.push_back({{{"role", "window"}, {"name", Text(window, kAXTitleAttribute)},
              {"application", DesktopText(app.localizedName.UTF8String ?: "")}, {"pid", app.processIdentifier}, {"foreground", foreground}, {"foreground_application", app.processIdentifier == front}},
            [element](const auto& operation, const auto& value) { return Perform(element, operation, value); },
            [element](int max, int levels) { return Tree(element, max, levels); }});
        }
      }
      if (raw) CFRelease(raw);
      if (result.items.size() >= 128) { result.truncated = true; break; }
    }
    if (!AXIsProcessTrusted()) throw std::runtime_error("ACCESSIBILITY_PERMISSION_REQUIRED");
    return result;
  }
}
nlohmann::json DesktopLaunch(const std::string& application) {
  @autoreleasepool {
    NSString* value = [NSString stringWithUTF8String:application.c_str()];
    NSURL* url = [value hasPrefix:@"/"] ? [NSURL fileURLWithPath:value] :
        [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:value];
    if (!url || ![url.path.pathExtension.lowercaseString isEqualToString:@"app"])
      throw std::runtime_error("APPLICATION_UNAVAILABLE");
    [NSWorkspace.sharedWorkspace openApplicationAtURL:url configuration:NSWorkspaceOpenConfiguration.configuration completionHandler:nil];
    return {{"accepted", true}, {"state", "requested"}};
  }
}
nlohmann::json DesktopOpenUrl(const std::string& value) {
  @autoreleasepool {
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:value.c_str()]];
    if (!url || ![NSWorkspace.sharedWorkspace openURL:url]) throw std::runtime_error("UI_ACTION_FAILED");
    return {{"accepted", true}};
  }
}
} // namespace crossdesk
