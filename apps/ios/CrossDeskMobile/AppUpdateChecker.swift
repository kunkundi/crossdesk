import Foundation
import Combine
import StoreKit

/// Main-thread checks of the version actually published on the App Store.
final class AppUpdateChecker: ObservableObject {
    enum Status { case idle, checking, upToDate, available, unavailable, requiresNewerOS, failed }
    // Fetchers complete asynchronously on the main thread.
    typealias Fetch = (@escaping (Data?) -> Void) -> (() -> Void)
    @Published private(set) var status: Status = .idle
    @Published private(set) var availableVersion = ""
    @Published private(set) var appStoreURL: URL?
    @Published private(set) var requiredOSVersion = ""
    var updateAvailable: Bool { !availableVersion.isEmpty && appStoreURL != nil }
    let currentVersion: String

    private let bundleIdentifier: String
    private let appStoreID: String
    private let systemVersion: String
    private let fetch: Fetch
    private let now: () -> Date
    private var nextAutomaticCheck: Date?
    private var statusBeforeCheck: Status = .idle
    private var enabled = false
    private var generation = UUID()
    private var cancelRequest: (() -> Void)?

    init(currentVersion: String = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "",
         bundleIdentifier: String = Bundle.main.bundleIdentifier ?? "",
         appStoreID: String = Bundle.main.object(forInfoDictionaryKey: "CrossDeskAppStoreID") as? String ?? "",
         systemVersion: String = AppUpdateChecker.currentSystemVersion,
         fetch: Fetch? = nil,
         now: @escaping () -> Date = Date.init) {
        self.currentVersion = currentVersion
        self.bundleIdentifier = bundleIdentifier
        self.appStoreID = appStoreID
        self.systemVersion = systemVersion
        self.fetch = fetch ?? { AppUpdateChecker.fetchRelease(appStoreID: appStoreID, completion: $0) }
        self.now = now
    }

    deinit {
        cancelRequest?()
    }

    func setEnabled(_ value: Bool) {
        guard enabled != value else { return }
        enabled = value
        if enabled { checkIfNeeded(); return }
        generation = UUID()
        cancelRequest?()
        cancelRequest = nil
        if status == .checking {
            status = statusBeforeCheck
        }
    }

    /// Reuse this run's result for six hours; failed requests can retry after 15 minutes.
    func checkIfNeeded() {
        if let nextAutomaticCheck, now() < nextAutomaticCheck { return }
        checkNow()
    }

    /// Manual checks bypass the automatic cooldown, but still require consent and foreground.
    func checkNow() {
        guard enabled, status != .checking else { return }
        statusBeforeCheck = status
        status = .checking
        let token = UUID()
        generation = token
        cancelRequest = fetch { [weak self] data in
            guard let self, self.enabled, self.generation == token else { return }
            self.cancelRequest = nil
            // A cancelled request does not consume the automatic-check interval.
            self.nextAutomaticCheck = self.now().addingTimeInterval(15 * 60)
            guard let data, let result = Self.parseRelease(data, bundleIdentifier: self.bundleIdentifier,
                                                          appStoreID: self.appStoreID) else {
                self.status = .failed
                return
            }
            switch result {
            case .unavailable:
                self.clearUpdate()
                self.status = .unavailable
            case .release(let release):
                guard let newer = Self.isNewerVersion(release.version, than: self.currentVersion),
                      let requiresNewerOS = Self.isNewerVersion(release.minimumOsVersion, than: self.systemVersion) else {
                    self.status = .failed
                    return
                }
                self.clearUpdate()
                if newer && requiresNewerOS {
                    self.requiredOSVersion = release.minimumOsVersion
                    self.status = .requiresNewerOS
                } else if newer {
                    self.availableVersion = release.version
                    self.appStoreURL = URL(string: "https://apps.apple.com/app/id\(release.trackId)")
                    self.status = .available
                } else {
                    self.status = .upToDate
                }
            }
            self.nextAutomaticCheck = self.now().addingTimeInterval(6 * 60 * 60)
        }
    }

    private func clearUpdate() {
        availableVersion = ""
        appStoreURL = nil
        requiredOSVersion = ""
    }

    struct StoreRelease: Decodable {
        let wrapperType: String
        let bundleId: String
        let trackId: Int64
        let version: String
        let minimumOsVersion: String
    }

    private struct LookupResponse: Decodable {
        let resultCount: Int
        let results: [StoreRelease]
    }

    enum LookupResult {
        case unavailable
        case release(StoreRelease)
    }

    static func parseRelease(_ data: Data, bundleIdentifier: String, appStoreID: String) -> LookupResult? {
        guard !bundleIdentifier.isEmpty, let identifier = numericAppID(appStoreID),
              data.count <= 256 * 1024,
              let response = try? JSONDecoder().decode(LookupResponse.self, from: data),
              response.resultCount == response.results.count else { return nil }
        if response.resultCount == 0 { return .unavailable }
        guard response.resultCount == 1, let release = response.results.first,
              release.wrapperType == "software", release.bundleId == bundleIdentifier,
              release.trackId == identifier,
              versionComponents(release.version) != nil,
              versionComponents(release.minimumOsVersion) != nil else { return nil }
        return .release(release)
    }

    /// App Store marketing versions are numeric; build numbers and desktop patches are unrelated.
    static func isNewerVersion(_ version: String, than current: String) -> Bool? {
        guard let latest = versionComponents(version), let installed = versionComponents(current) else { return nil }
        return installed.lexicographicallyPrecedes(latest)
    }

    private static func versionComponents(_ version: String) -> [UInt64]? {
        let parts = version.split(separator: ".", omittingEmptySubsequences: false)
        guard (1...3).contains(parts.count) else { return nil }
        var components: [UInt64] = []
        for part in parts {
            guard !part.isEmpty, part.utf8.allSatisfy({ (48...57).contains($0) }),
                  let value = UInt64(part) else { return nil }
            components.append(value)
        }
        return components + Array(repeating: 0, count: 3 - components.count)
    }

    private static func numericAppID(_ value: String) -> Int64? {
        guard !value.isEmpty, value.utf8.allSatisfy({ (48...57).contains($0) }),
              let identifier = Int64(value), identifier > 0 else { return nil }
        return identifier
    }

    static var currentSystemVersion: String {
        let version = ProcessInfo.processInfo.operatingSystemVersion
        return "\(version.majorVersion).\(version.minorVersion).\(version.patchVersion)"
    }

    static func lookupCountry(storefrontCountry: String?, fallbackRegion: String?) -> String? {
        // StoreKit uses ISO alpha-3; Lookup expects alpha-2. Foundation canonicalizes aliases.
        guard let source = storefrontCountry ?? fallbackRegion else { return nil }
        let country = Locale(identifier: "und_" + source).region?.identifier.lowercased() ?? ""
        guard country.utf8.count == 2, country.utf8.allSatisfy({ (97...122).contains($0) }) else { return nil }
        return country
    }

    static func lookupURL(appStoreID: String, country: String) -> URL? {
        guard let identifier = numericAppID(appStoreID),
              country.utf8.count == 2, country.utf8.allSatisfy({ (97...122).contains($0) }) else { return nil }
        var url = URLComponents(string: "https://itunes.apple.com/lookup")!
        url.queryItems = [URLQueryItem(name: "id", value: String(identifier)),
                          URLQueryItem(name: "country", value: country),
                          URLQueryItem(name: "entity", value: "software")]
        return url.url
    }

    // Only the public app ID and storefront region are sent; no installed version or credentials.
    private static func fetchRelease(appStoreID: String, completion: @escaping (Data?) -> Void) -> (() -> Void) {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 10
        configuration.timeoutIntervalForResource = 15
        let session = URLSession(configuration: configuration)
        let task = Task { @MainActor in
            defer { session.finishTasksAndInvalidate() }
            let storefront = await Storefront.current
            guard !Task.isCancelled else { return }
            guard let country = lookupCountry(storefrontCountry: storefront?.countryCode,
                                              fallbackRegion: Locale.current.region?.identifier),
                  let url = lookupURL(appStoreID: appStoreID, country: country) else {
                completion(nil)
                return
            }
            do {
                let request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData)
                let (data, response) = try await session.data(for: request)
                guard !Task.isCancelled else { return }
                let valid = (response as? HTTPURLResponse)?.statusCode == 200 && data.count <= 256 * 1024
                completion(valid ? data : nil)
            } catch {
                if !Task.isCancelled { completion(nil) }
            }
        }
        return { task.cancel(); session.invalidateAndCancel() }
    }
}
