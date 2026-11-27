import Foundation
import CryptoKit

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
    var credentialRef: String?
    var fingerprint: String?
    var usesLegacyPassword: Bool?
    var platform: RemoteHostPlatform

    var id: String { remoteID }
    var boundFingerprint: String? {
        guard let fingerprint, fingerprint.utf8.count == 64,
              fingerprint.utf8.allSatisfy({ (48...57).contains($0) || (97...102).contains($0) }) else { return nil }
        return fingerprint
    }
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
        case credentialRef, fingerprint
        case remoteID, alias, groups, lastConnectedAt, remembersPassword, thumbnailFileName, platform, usesLegacyPassword
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
        credentialRef = try? values.decode(String.self, forKey: .credentialRef)
        fingerprint = try? values.decode(String.self, forKey: .fingerprint)
        usesLegacyPassword = try values.decodeIfPresent(Bool.self, forKey: .usesLegacyPassword)
        // Adding platform metadata must not discard records saved by older apps.
        platform = (try? values.decode(RemoteHostPlatform.self, forKey: .platform)) ?? .unknown
    }

    mutating func updateHostInfo(name: String, platform: RemoteHostPlatform) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmed.isEmpty { hostName = trimmed }
        self.platform = platform
    }
}


struct ConnectionHistory: Equatable {
    private static let prefix = "crossdesk.mobile.recent-connections.v2."
    private static let legacyKey = "crossdesk.mobile.recent-connections.v1"
    private static let legacyOwnerKey = "crossdesk.mobile.recent-connections.first-server"
    let key: String

    private init(key: String) { self.key = key }

    init(endpoint: String) {
        key = Self.prefix + Self.digest(endpoint)
    }

    func adoptLegacyHistory() {
        let defaults = UserDefaults.standard
        guard let data = defaults.data(forKey: Self.legacyKey) else { return }
        let owner = defaults.string(forKey: Self.legacyOwnerKey) ?? key
        defaults.set(owner, forKey: Self.legacyOwnerKey)
        guard let legacy = try? JSONDecoder().decode([RecentConnection].self, from: data) else { return }
        let destination = Self(key: owner)
        var records = destination.load()
        var ids = Set(records.map(\.id))
        for var record in legacy where ids.insert(record.id).inserted {
            record.thumbnailFileName = nil
            // Keep the original Keychain item accessible even when protected
            // data is locked at launch. Only this server inherits the reference.
            record.usesLegacyPassword = record.remembersPassword
            records.append(record)
        }
        if destination.save(records) { defaults.removeObject(forKey: Self.legacyKey) }
    }

    private static func digest(_ text: String) -> String {
        SHA256.hash(data: Data(text.utf8)).map { String(format: "%02x", $0) }.joined()
    }

    func account(for id: String) -> String { key + "/" + id }

    func thumbnailName(for id: String) -> String {
        "v2-" + Self.digest(account(for: id)) + ".jpg"
    }

    func load() -> [RecentConnection] {
        guard let data = UserDefaults.standard.data(forKey: key),
              let records = try? JSONDecoder().decode([RecentConnection].self, from: data)
        else { return [] }
        return records.sorted { $0.lastConnectedAt > $1.lastConnectedAt }
    }

    @discardableResult
    func save(_ records: [RecentConnection]) -> Bool {
        guard let data = try? JSONEncoder().encode(records) else { return false }
        UserDefaults.standard.set(data, forKey: key)
        return true
    }

    static func clearThumbnailReferences() {
        for key in UserDefaults.standard.dictionaryRepresentation().keys
            where key.hasPrefix(prefix) {
            let history = Self(key: key)
            var records = history.load()
            guard records.contains(where: { $0.thumbnailFileName != nil }) else { continue }
            for index in records.indices { records[index].thumbnailFileName = nil }
            history.save(records)
        }
    }
}
