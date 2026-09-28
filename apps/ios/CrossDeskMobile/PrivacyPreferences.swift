import Foundation

/// Consent is deliberately independent of existing device IDs and credentials.
/// A missing value (including an upgrade from older versions) is not consent.
struct PrivacyPreferences {
    private let defaults: UserDefaults
    private let networkKey = "crossdesk.mobile.network-consent.v1"
    private let thumbnailsKey = "crossdesk.mobile.save-thumbnails.v1"

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    var hasNetworkConsent: Bool { defaults.bool(forKey: networkKey) }
    var savesThumbnails: Bool {
        hasNetworkConsent && defaults.bool(forKey: thumbnailsKey)
    }

    func setNetworkConsent(_ allowed: Bool) {
        defaults.set(allowed, forKey: networkKey)
        if !allowed { defaults.set(false, forKey: thumbnailsKey) }
    }

    func setSavesThumbnails(_ allowed: Bool) {
        defaults.set(allowed && hasNetworkConsent, forKey: thumbnailsKey)
    }
}

/// Refusal dismisses the notice for this visit only. A background/foreground
/// round trip starts another visit; transient inactive states do not.
struct PrivacyNoticeState {
    private(set) var isVisible: Bool
    private var needsPresentationOnActivation = false

    init(hasNetworkConsent: Bool) {
        isVisible = !hasNetworkConsent
    }

    mutating func show() {
        isVisible = true
    }

    mutating func dismiss() {
        isVisible = false
    }

    mutating func didEnterBackground() {
        needsPresentationOnActivation = true
    }

    mutating func didBecomeActive(hasNetworkConsent: Bool) {
        guard needsPresentationOnActivation else { return }
        needsPresentationOnActivation = false
        isVisible = !hasNetworkConsent
    }
}
