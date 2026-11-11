import Foundation

// Values match crossdesk::VideoSettings on the reliable control stream.
struct RemoteVideoSettings: Equatable {
    var quality = 2
    var frameRate = 60
    var preference = 1

    var isValid: Bool {
        (0...2).contains(quality) && [30, 60].contains(frameRate) &&
            (0...2).contains(preference)
    }
}

struct RemoteVideoSettingsState {
    private(set) var supported = false
    private(set) var selection: RemoteVideoSettings
    private(set) var applied: RemoteVideoSettings
    private(set) var requestID: UInt32 = 0
    private(set) var pending = false
    private(set) var failed = false
    private var appliedRequestID: UInt32 = 0

    init(preference: Int = 1) {
        let settings = RemoteVideoSettings(preference: preference)
        selection = settings
        applied = settings
    }

    mutating func setSupported(_ value: Bool) {
        supported = value
        if !value {
            selection = applied
            pending = false
            failed = false
        }
    }

    mutating func begin(_ settings: RemoteVideoSettings) -> UInt32? {
        guard supported, settings.isValid else { return nil }
        requestID &+= 1
        selection = settings
        pending = true
        failed = false
        return requestID
    }

    mutating func receive(_ settings: RemoteVideoSettings,
                          requestID responseID: UInt32, accepted: Bool) {
        guard supported, settings.isValid,
              Int32(bitPattern: requestID &- responseID) >= 0 else { return }
        // A superseded success still establishes the rollback value for a
        // newer request. A stale response must never replace its selection.
        if accepted, Int32(bitPattern: responseID &- appliedRequestID) >= 0 {
            applied = settings
            appliedRequestID = responseID
        }
        guard responseID == requestID else { return }
        pending = false
        failed = !accepted
        selection = applied
    }

    mutating func expire(requestID expiredID: UInt32) {
        guard pending, expiredID == requestID else { return }
        selection = applied
        pending = false
        failed = true
    }
}


struct RemoteDisplayResolution: Codable, Equatable, Hashable {
    let width: Int
    let height: Int
    var label: String { "\(width) × \(height)" }
    var valid: Bool { (1...32768).contains(width) && (1...32768).contains(height) }
}

struct RemoteDisplayResolutionPreference: Codable, Equatable {
    let display_key: String
    let width: Int
    let height: Int
    var valid: Bool {
        !display_key.isEmpty && display_key.utf8.count <= 1024 && !display_key.contains("\0") &&
            RemoteDisplayResolution(width: width, height: height).valid
    }
}

enum RemoteDisplayResolutionStore {
    static func key(host: String, port: Int, remoteID: String) -> String {
        let scope = [host.trimmingCharacters(in: .whitespacesAndNewlines).lowercased(), String(port), remoteID]
        guard !remoteID.isEmpty, let data = try? JSONEncoder().encode(scope) else { return "" }
        return "crossdesk.remoteDisplayResolutions.v1." + data.base64EncodedString()
    }
    static func load(key: String, defaults: UserDefaults = .standard) -> [RemoteDisplayResolutionPreference] {
        guard !key.isEmpty, let data = defaults.data(forKey: key), data.count <= 65536,
              let values = try? JSONDecoder().decode([RemoteDisplayResolutionPreference].self, from: data),
              values.count <= 32, values.allSatisfy(\.valid),
              Set(values.map(\.display_key)).count == values.count else { return [] }
        return values
    }
    static func remember(_ preference: RemoteDisplayResolutionPreference, key: String,
                         defaults: UserDefaults = .standard) {
        guard !key.isEmpty, preference.valid else { return }
        var values = load(key: key, defaults: defaults).filter { $0.display_key != preference.display_key }
        if values.count >= 32 { values.removeFirst() }
        values.append(preference)
        if let data = try? JSONEncoder().encode(values) { defaults.set(data, forKey: key) }
    }
    static func message(key: String, defaults: UserDefaults = .standard) -> Data {
        (try? JSONEncoder().encode(load(key: key, defaults: defaults))) ?? Data("[]".utf8)
    }
}

struct RemoteDisplayResolutionInfo: Decodable {
    let display_id: Int
    let display_key: String
    let width: Int
    let height: Int
    let modes: [RemoteDisplayResolution]
    var current: RemoteDisplayResolution { RemoteDisplayResolution(width: width, height: height) }
    var valid: Bool {
        (0..<32).contains(display_id) && !display_key.isEmpty &&
            display_key.utf8.count <= 1024 && !display_key.contains("\0") &&
            current.valid && modes.count <= 256 && modes.allSatisfy(\.valid)
    }
}

struct RemoteDisplayResolutionRequest: Decodable, Equatable {
    let display_id: Int
    let display_key: String
    let width: Int
    let height: Int
    let request_id: UInt32
    var accepted = false
    var resolution: RemoteDisplayResolution { RemoteDisplayResolution(width: width, height: height) }
    func matches(_ other: Self) -> Bool {
        display_id == other.display_id && display_key == other.display_key &&
            resolution == other.resolution && request_id == other.request_id
    }
}

struct RemoteDisplayResolutionState {
    private(set) var displays: [RemoteDisplayResolutionInfo] = []
    private(set) var requested: RemoteDisplayResolutionRequest?
    private(set) var pending = false
    private(set) var failed = false

    func display(_ id: Int) -> RemoteDisplayResolutionInfo? {
        displays.first { $0.display_id == id }
    }
    mutating func begin(displayID: Int, resolution: RemoteDisplayResolution) -> RemoteDisplayResolutionRequest? {
        guard !pending, let display = display(displayID), display.modes.contains(resolution),
              display.current != resolution else { return nil }
        let next = (requested?.request_id ?? 0) &+ 1
        let request = RemoteDisplayResolutionRequest(display_id: displayID,
            display_key: display.display_key, width: resolution.width,
            height: resolution.height, request_id: next == 0 ? 1 : next)
        requested = request; pending = true; failed = false
        return request
    }
    mutating func expire(requestID: UInt32) {
        guard pending, requested?.request_id == requestID else { return }
        pending = false; failed = true
    }
    @discardableResult
    mutating func receive(_ data: Data) -> RemoteDisplayResolutionPreference? {
        struct Host: Decodable { let display_resolutions: [RemoteDisplayResolutionInfo]? }
        struct Message: Decodable {
            let type: Int
            let host_info: Host?
            let display_resolution: RemoteDisplayResolutionRequest?
            let display_resolutions: [RemoteDisplayResolutionInfo]?
        }
        guard let message = try? JSONDecoder().decode(Message.self, from: data) else { return nil }
        let actual: [RemoteDisplayResolutionInfo]
        if message.type == 3, let host = message.host_info {
            actual = host.display_resolutions ?? []
        } else if message.type == 16, let response = message.display_resolution,
                  let requested, response.matches(requested), let values = message.display_resolutions {
            actual = values
        } else { return nil }
        guard actual.count <= 32, actual.allSatisfy(\.valid),
              Set(actual.map(\.display_id)).count == actual.count else { return nil }
        displays = actual
        if message.type == 16, let response = message.display_resolution {
            pending = false
            failed = !response.accepted || !actual.contains {
                $0.display_id == response.display_id && $0.display_key == response.display_key &&
                    $0.current == response.resolution
            }
            if !failed {
                return RemoteDisplayResolutionPreference(display_key: response.display_key,
                    width: response.width, height: response.height)
            }
        } else if pending, let requested, !actual.contains(where: {
            $0.display_id == requested.display_id && $0.display_key == requested.display_key &&
                $0.modes.contains(requested.resolution)
        }) {
            expire(requestID: requested.request_id)
        }
        return nil
    }
}
