import Foundation
import Combine

struct RemoteShortcut: Codable, Identifiable, Equatable {
    let id: String
    let name: String
    let platform: String
    let group: String
    let key: UInt
    let modifiers: [UInt]
    var service = false
    var custom = false

    var title: String { custom ? name : NSLocalizedString(name, comment: "") }
    var chord: String {
        (modifiers.map { Self.modifierName($0, platform: platform) } +
         [Self.keyName(key, platform: platform)]).joined(separator: " + ")
    }
    static let platforms = ["windows", "macos", "linux"]
    static let modifierKeys: [UInt] = [17, 16, 18, 91]
    static let keys: [UInt] = Array(65...90) + Array(48...57) + Array(112...123) +
        [27, 9, 13, 32, 8, 46, 45, 36, 35, 33, 34, 37, 38, 39, 40]
    static func modifierName(_ key: UInt, platform: String) -> String {
        switch key {
        case 17: return "Ctrl"
        case 16: return "Shift"
        case 18: return platform == "macos" ? "Option" : "Alt"
        case 91: return platform == "macos" ? "Cmd" : platform == "linux" ? "Super" : "Win"
        default: return ""
        }
    }
    static func keyName(_ key: UInt, platform: String) -> String {
        if (65...90).contains(key) || (48...57).contains(key) { return String(UnicodeScalar(UInt32(key))!) }
        if (112...123).contains(key) { return "F\(key - 111)" }
        switch key {
        case 8: return platform == "macos" ? "Delete ⌫" : "Backspace"
        case 9: return "Tab"
        case 13: return "Enter"
        case 27: return "Esc"
        case 32: return "Space"
        case 33: return "PageUp"
        case 34: return "PageDown"
        case 35: return "End"
        case 36: return "Home"
        case 37: return "←"
        case 38: return "↑"
        case 39: return "→"
        case 40: return "↓"
        case 45: return "Insert"
        case 46: return platform == "macos" ? "Delete ⌦" : "Delete"
        default: return ""
        }
    }
    static func platformName(_ platform: String) -> String {
        switch platform {
        case "windows": return "Windows"
        case "macos": return "macOS"
        case "linux": return "Linux"
        default: return NSLocalizedString("未知", comment: "")
        }
    }
    var valid: Bool {
        Self.platforms.contains(platform) && id.hasPrefix("custom-") &&
        !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.count <= 64 &&
        Self.keys.contains(key) && modifiers.count <= 4 &&
        modifiers.allSatisfy(Self.modifierKeys.contains) &&
        !(platform == "windows" && key == 46 && modifiers.contains(17) && modifiers.contains(18))
    }
}

final class RemoteShortcutStore: ObservableObject {
    @Published private(set) var revision = 0
    private let defaults: UserDefaults
    private let prefix = "remote_shortcuts_v1."
    init(defaults: UserDefaults = .standard) { self.defaults = defaults }
    var desktop: String {
        get { let value = defaults.string(forKey: prefix + "linux_desktop") ?? ""; return ["gnome", "kde"].contains(value) ? value : "" }
        set { defaults.set(["gnome", "kde"].contains(newValue) ? newValue : "", forKey: prefix + "linux_desktop"); revision &+= 1 }
    }
    var custom: [RemoteShortcut] {
        guard let data = defaults.data(forKey: prefix + "custom"),
              let decoded = try? JSONDecoder().decode([RemoteShortcut].self, from: data) else { return [] }
        var seen = Set<String>()
        return Array(decoded.filter { $0.valid && $0.custom && !$0.service && seen.insert($0.id).inserted }.prefix(16))
    }
    func items(platform: String) -> [RemoteShortcut] {
        func item(_ id: String, _ name: String, _ group: String, _ key: UInt, _ modifiers: UInt...) -> RemoteShortcut {
            RemoteShortcut(id: id, name: name, platform: platform, group: group, key: key, modifiers: modifiers)
        }
        var result: [RemoteShortcut] = []
        if RemoteShortcut.platforms.contains(platform) {
            let primary: UInt = platform == "macos" ? 91 : 17
            let names = ["复制", "粘贴", "剪切", "全选", "撤销", "保存"]
            let ids = ["copy", "paste", "cut", "all", "undo", "save"]
            let keys: [UInt] = [67, 86, 88, 65, 90, 83]
            for index in ids.indices { result.append(item(ids[index], names[index], "common", keys[index], primary)) }
            if platform == "linux" {
                result += [item("term-copy", "终端复制", "common", 67, 17, 16), item("term-paste", "终端粘贴", "common", 86, 17, 16)]
            }
        }
        let keys: [UInt] = [27, 9, 13, 8, 46, 36, 35, 33, 34, 37, 38, 39, 40] + Array(112...123)
        for key in keys {
            result.append(item(key == 27 ? "esc" : key == 9 ? "tab" : "key-\(key)", RemoteShortcut.keyName(key, platform: platform), "keys", key))
        }
        if platform == "windows" {
            result += [item("switch", "切换窗口", "system", 9, 18), item("desktop", "显示桌面", "system", 68, 91), item("task", "任务管理器", "system", 27, 17, 16)]
            result.append(RemoteShortcut(id: "sas", name: "安全选项", platform: platform, group: "system", key: 46, modifiers: [17, 18], service: true))
        } else if platform == "macos" {
            result += [item("switch", "切换应用", "system", 9, 91), item("search", "聚焦搜索", "system", 32, 91), item("forcequit", "强制退出面板", "system", 27, 18, 91)]
        } else if platform == "linux" && !desktop.isEmpty {
            result += [item("switch", desktop == "gnome" ? "切换应用" : "切换窗口", "system", 9, desktop == "gnome" ? 91 : 18), item("run", "运行窗口", "system", 113, 18)]
            result.append(desktop == "gnome" ? item("apps", "应用列表", "system", 65, 91) : item("window-menu", "窗口菜单", "system", 114, 18))
        }
        return result + custom.filter { $0.platform == platform }
    }
    func pins(platform: String) -> [String] {
        let values = defaults.stringArray(forKey: prefix + "pins_" + platform) ?? ["esc", "tab", "copy", "paste"]
        let available = Set(items(platform: platform).map(\.id))
        var seen = Set<String>()
        return Array(values.filter { available.contains($0) && seen.insert($0).inserted }.prefix(8))
    }
    func pinned(platform: String) -> [RemoteShortcut] {
        let all = items(platform: platform)
        return pins(platform: platform).compactMap { id in all.first { $0.id == id } }
    }
    func setPins(_ values: [String], platform: String) {
        defaults.set(Array(values.prefix(8)), forKey: prefix + "pins_" + platform); revision &+= 1
    }
    func remove(_ id: String) {
        write(custom.filter { $0.id != id })
        for platform in RemoteShortcut.platforms { setPins(pins(platform: platform).filter { $0 != id }, platform: platform) }
    }
    func save(id: String?, name: String, platform: String, key: UInt, modifiers: [UInt], pin: Bool) -> String? {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let normalized = RemoteShortcut.modifierKeys.filter(modifiers.contains)
        let item = RemoteShortcut(id: id ?? "custom-" + UUID().uuidString, name: name, platform: platform, group: "mine", key: key, modifiers: normalized, custom: true)
        if platform == "windows" && key == 46 && normalized.contains(17) && normalized.contains(18) { return NSLocalizedString("请使用系统预设中的安全选项", comment: "") }
        guard item.valid else { return NSLocalizedString("请输入有效的快捷键名称", comment: "") }
        var values = custom
        if values.contains(where: { $0.id != id && $0.platform == platform && $0.key == key && Set($0.modifiers) == Set(normalized) }) { return NSLocalizedString("已有相同组合的快捷键", comment: "") }
        if !values.contains(where: { $0.id == id }) && values.count >= 16 { return NSLocalizedString("最多保存 16 个自定义快捷键", comment: "") }
        if pin && !pins(platform: platform).contains(item.id) && pins(platform: platform).count >= 8 { return NSLocalizedString("最多固定 8 个快捷键", comment: "") }
        values.removeAll { $0.id == id }; values.append(item); write(values)
        for target in RemoteShortcut.platforms {
            var selected = pins(platform: target); let previous = selected.firstIndex(of: item.id)
            selected.removeAll { $0 == item.id }
            if target == platform && pin { selected.insert(item.id, at: min(previous ?? selected.count, selected.count)) }
            setPins(selected, platform: target)
        }
        return nil
    }
    private func write(_ values: [RemoteShortcut]) {
        guard let data = try? JSONEncoder().encode(values) else { return }
        defaults.set(data, forKey: prefix + "custom"); revision &+= 1
    }
}
