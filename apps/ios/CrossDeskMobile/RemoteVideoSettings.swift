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
