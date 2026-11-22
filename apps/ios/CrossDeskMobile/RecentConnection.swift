import Foundation

enum RemoteHostPlatform: String, Codable {
    case unknown, windows, macos, linux, ios

    var displayName: String {
        switch self {
        case .windows: return "Windows"
        case .macos: return "macOS"
        case .linux: return "Linux"
        case .ios: return "iOS"
        case .unknown: return NSLocalizedString("未知", comment: "")
        }
    }
}

struct RecentConnection: Codable, Identifiable, Equatable {
    let remoteID: String
    var hostName: String
    var alias: String
    var groups: [String]
    var lastConnectedAt: Date
    var remembersPassword: Bool
    var thumbnailFileName: String?
    var platform: RemoteHostPlatform

    var id: String { remoteID }
    var displayName: String { alias.isEmpty ? hostName : alias }

    init(remoteID: String, displayName: String, lastConnectedAt: Date,
         remembersPassword: Bool, thumbnailFileName: String?,
         platform: RemoteHostPlatform = .unknown, alias: String = "", groups: [String] = []) {
        self.remoteID = remoteID
        self.hostName = displayName
        self.alias = alias
        self.groups = groups
        self.lastConnectedAt = lastConnectedAt
        self.remembersPassword = remembersPassword
        self.thumbnailFileName = thumbnailFileName
        self.platform = platform
    }

    private enum CodingKeys: String, CodingKey {
        case hostName = "displayName"
        case remoteID, alias, groups, lastConnectedAt, remembersPassword, thumbnailFileName, platform
    }

    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        remoteID = try values.decode(String.self, forKey: .remoteID)
        hostName = try values.decode(String.self, forKey: .hostName)
        alias = try values.decodeIfPresent(String.self, forKey: .alias) ?? ""
        groups = try values.decodeIfPresent([String].self, forKey: .groups) ?? []
        lastConnectedAt = try values.decode(Date.self, forKey: .lastConnectedAt)
        remembersPassword = try values.decode(Bool.self, forKey: .remembersPassword)
        thumbnailFileName = try values.decodeIfPresent(String.self, forKey: .thumbnailFileName)
        // Adding platform metadata must not discard records saved by older apps.
        platform = (try? values.decode(RemoteHostPlatform.self, forKey: .platform)) ?? .unknown
    }

    mutating func updateHostInfo(name: String, platform: RemoteHostPlatform) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmed.isEmpty { hostName = trimmed }
        self.platform = platform
    }
}
