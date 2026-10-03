import Foundation
import Combine

/// Main-thread local-app checks triggered by visits, session endings and manual requests.
final class AppUpdateChecker: ObservableObject {
    enum Status { case idle, checking, upToDate, available, failed }
    // Fetchers complete asynchronously on the main thread.
    typealias Fetch = (@escaping (Data?) -> Void) -> (() -> Void)
    @Published private(set) var status: Status = .idle
    @Published private(set) var availableVersion = ""
    var updateAvailable: Bool { !availableVersion.isEmpty }
    let currentVersion: String

    private let fetch: Fetch
    private let compare: (String, Data) -> String?
    private var enabled = false
    private var generation = UUID()
    private var cancelRequest: (() -> Void)?

    init(currentVersion: String = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "",
         fetch: @escaping Fetch = AppUpdateChecker.fetchRelease,
         compare: @escaping (String, Data) -> String? = CrossDeskRTCBridge.checkMobileUpdate) {
        self.currentVersion = currentVersion
        self.fetch = fetch
        self.compare = compare
    }

    deinit {
        cancelRequest?()
    }

    func setEnabled(_ value: Bool) {
        guard enabled != value else { return }
        enabled = value
        if enabled { checkNow(); return }
        generation = UUID()
        cancelRequest?()
        cancelRequest = nil
        if status == .checking {
            status = updateAvailable ? .available : .idle
        }
    }

    func checkNow() {
        guard enabled, status != .checking else { return }
        status = .checking
        let token = UUID()
        generation = token
        cancelRequest = fetch { [weak self] data in
            guard let self, self.enabled, self.generation == token else { return }
            self.cancelRequest = nil
            if let data, let result = self.compare(self.currentVersion, data) {
                self.availableVersion = result
                self.status = self.updateAvailable ? .available : .upToDate
            } else {
                self.status = .failed
            }
        }
    }

    // Public metadata only: no identity, installed version or credentials sent.
    private static func fetchRelease(completion: @escaping (Data?) -> Void) -> (() -> Void) {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 10
        configuration.timeoutIntervalForResource = 15
        let session = URLSession(configuration: configuration)
        let request = URLRequest(url: URL(string: "https://version.crossdesk.cn/version.json")!,
                                 cachePolicy: .reloadIgnoringLocalCacheData)
        let task = session.dataTask(with: request) { data, response, error in
            let valid = error == nil && (response as? HTTPURLResponse)?.statusCode == 200 &&
                (data?.count ?? 0) <= 256 * 1024
            DispatchQueue.main.async { completion(valid ? data : nil) }
            session.finishTasksAndInvalidate()
        }
        task.resume()
        return { task.cancel(); session.invalidateAndCancel() }
    }
}
