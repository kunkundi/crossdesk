import Foundation

enum RemoteHostPlatform: String, Codable {
    case unknown, windows, macos, linux, ios

    var displayName: String {
        switch self {
        case .windows: return "Windows"
        case .macos: return "macOS"
        case .linux: return "Linux"
        case .ios: return "iOS"
        case .unknown: return "未知"
        }
    }
}

struct RecentConnection: Codable, Identifiable, Equatable {
    let remoteID: String
    var displayName: String
    var lastConnectedAt: Date
    var remembersPassword: Bool
    var thumbnailFileName: String?
    var platform: RemoteHostPlatform

    var id: String { remoteID }

    init(remoteID: String, displayName: String, lastConnectedAt: Date,
         remembersPassword: Bool, thumbnailFileName: String?,
         platform: RemoteHostPlatform = .unknown) {
        self.remoteID = remoteID
        self.displayName = displayName
        self.lastConnectedAt = lastConnectedAt
        self.remembersPassword = remembersPassword
        self.thumbnailFileName = thumbnailFileName
        self.platform = platform
    }

    private enum CodingKeys: String, CodingKey {
        case remoteID, displayName, lastConnectedAt, remembersPassword, thumbnailFileName, platform
    }

    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        remoteID = try values.decode(String.self, forKey: .remoteID)
        displayName = try values.decode(String.self, forKey: .displayName)
        lastConnectedAt = try values.decode(Date.self, forKey: .lastConnectedAt)
        remembersPassword = try values.decode(Bool.self, forKey: .remembersPassword)
        thumbnailFileName = try values.decodeIfPresent(String.self, forKey: .thumbnailFileName)
        // Adding platform metadata must not discard records saved by older apps.
        platform = (try? values.decode(RemoteHostPlatform.self, forKey: .platform)) ?? .unknown
    }

    mutating func updateHostInfo(name: String, platform: RemoteHostPlatform) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmed.isEmpty { displayName = trimmed }
        self.platform = platform
    }
}
