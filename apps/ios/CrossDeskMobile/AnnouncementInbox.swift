import Combine
import CryptoKit
import Foundation

struct Announcement: Decodable, Identifiable, Equatable {
    let id: Int64
    let revision: Int64
    let title: String
    let body: String
    let updated_at: Int64
    var read = false

    // Reading state belongs to this device, never to the server response.
    private enum CodingKeys: String, CodingKey {
        case id, revision, title, body, updated_at
    }

    var date: Date { Date(timeIntervalSince1970: TimeInterval(updated_at)) }
}

struct AnnouncementSnapshot: Equatable {
    var items: [Announcement] = []
    var total = 0
    var unread = 0
    var loaded = false
    var loading = false
    var failed = false
    var readSaveFailed = false
    var deleteFailed = false
}

/// Main-thread inbox using the desktop announcements_list protocol and local
/// revision semantics. Summary pages count all unread items; bodies load in 20s.
final class AnnouncementInbox: ObservableObject {
    @Published private(set) var state = AnnouncementSnapshot()
    var send: ((Data, String) -> Void)?

    private struct Entry: Decodable {
        let id: Int64
        let revision: Int64
    }

    private struct Response: Decodable {
        let request_id: String
        let summary_only: Bool
        let offset: Int
        let total: Int
        let catalog_revision: Int64
        let items: [Entry]
    }

    private struct BodyResponse: Decodable { let items: [Announcement] }
    private struct Request: Encodable {
        let type = "announcements_list"
        let request_id: String
        let summary_only: Bool
        let offset: Int
    }

    private struct LocalState: Codable {
        let version: Int
        let scope: String
        let reads: [String: Int64]
        let dismissed: [String: Int64]?
    }

    private let directory: URL
    private var scope = ""
    private var file: URL?
    private var reads: [Int64: Int64] = [:]
    private var dismissed: [Int64: Int64] = [:]
    private var catalog: [Int64: Int64] = [:]
    private var pendingCatalog: [Int64: Int64] = [:]
    private var pageIDs: [Int: [Int64]] = [:]
    private var pageBodies: [Int: [Announcement]] = [:]
    private var catalogRevision: Int64 = -1
    private var pendingRevision: Int64 = -1
    private var pendingTotal = 0
    private var pageOffset = 0
    private var visibleLimit = 20
    private var summary = true
    private var connected = false
    private var requestID: String?
    private var timeout: Timer?

    init(directory: URL = FileManager.default.urls(
        for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("AnnouncementReads", isDirectory: true)) {
        self.directory = directory
    }

    deinit { timeout?.invalidate() }

    func configure(host: String, port: Int, deviceID: String) {
        guard !host.isEmpty, port > 0, !deviceID.isEmpty else { return }
        var host = host.lowercased()
        if host.hasSuffix(".") { host.removeLast() }
        guard let data = try? JSONSerialization.data(withJSONObject: [host, port, deviceID],
                                                     options: [.fragmentsAllowed]),
              let nextScope = String(data: data, encoding: .utf8),
              scope != nextScope else { return }
        let wasConnected = connected
        reset()
        connected = wasConnected
        scope = nextScope
        let name = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        file = directory.appendingPathComponent(name + ".json")
        loadLocalState()
        refresh()
    }

    func reset() {
        cancelRequest()
        state = AnnouncementSnapshot()
        scope = ""
        file = nil
        reads = [:]
        dismissed = [:]
        catalog = [:]
        pendingCatalog = [:]
        pageIDs = [:]
        pageBodies = [:]
        catalogRevision = -1
        pendingRevision = -1
        pendingTotal = 0
        visibleLimit = 20
        connected = false
    }

    func setConnected(_ value: Bool) {
        connected = value
        refresh()
    }

    func refresh() {
        cancelRequest()
        pendingCatalog = [:]
        pendingRevision = -1
        pendingTotal = 0
        summary = true
        request(offset: 0)
    }

    func loadMore() {
        guard connected, !state.loading, !state.failed, catalogRevision >= 0,
              visibleLimit < state.total else { return }
        visibleLimit += min(20, state.total - visibleLimit)
        rebuildVisibleItems()
    }

    func markRead(_ item: Announcement) {
        guard state.items.contains(where: { $0.id == item.id && $0.revision == item.revision }),
              reads[item.id] != item.revision else { return }
        var updated = reads
        updated[item.id] = item.revision
        guard saveLocalState(reads: updated, dismissed: dismissed) else {
            state.readSaveFailed = true
            return
        }
        reads = updated
        state.readSaveFailed = false
        state.deleteFailed = false
        updateReadingState()
    }

    func dismiss(_ item: Announcement) {
        // A confirmation can outlive the displayed revision.
        guard state.items.contains(where: { $0.id == item.id && $0.revision == item.revision }) else {
            state.deleteFailed = true
            return
        }
        var updated = dismissed
        updated[item.id] = item.revision
        guard saveLocalState(reads: reads, dismissed: updated) else {
            state.deleteFailed = true
            return
        }
        dismissed = updated
        state.deleteFailed = false
        state.readSaveFailed = false
        if summary && state.loading {
            state.items.removeAll { dismissed[$0.id] == $0.revision }
            updateReadingState()
        } else {
            cancelRequest()
            rebuildVisibleItems()
        }
    }

    func failed(requestID: String) {
        guard self.requestID == requestID else { return }
        cancelRequest()
        state.failed = true
    }

    func receive(_ data: Data) {
        guard let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              let id = object["request_id"] as? String, id == requestID else { return }
        let decoder = JSONDecoder()
        guard object["error"] == nil, let response = try? decoder.decode(Response.self, from: data) else {
            failed(requestID: id)
            return
        }
        let limit = summary ? 200 : 20
        guard response.summary_only == summary, response.offset >= 0,
              response.total >= response.offset, response.total <= Int32.max,
              response.catalog_revision >= 0,
              response.items.count == min(limit, response.total - response.offset),
              response.items.allSatisfy({ $0.id > 0 && $0.revision > 0 }) else {
            failed(requestID: id)
            return
        }
        let expectedRevision = summary ? pendingRevision : catalogRevision
        if expectedRevision >= 0 && expectedRevision != response.catalog_revision {
            refresh()
            return
        }
        if summary {
            guard response.offset == pendingCatalog.count,
                  pendingRevision < 0 || pendingTotal == response.total else {
                failed(requestID: id)
                return
            }
            for item in response.items {
                guard pendingCatalog.updateValue(item.revision, forKey: item.id) == nil else {
                    failed(requestID: id)
                    return
                }
            }
            pendingRevision = response.catalog_revision
            pendingTotal = response.total
            cancelRequest()
            if pendingCatalog.count < pendingTotal {
                request(offset: pendingCatalog.count)
                return
            }
            if catalogRevision != pendingRevision {
                pageIDs = [:]
                pageBodies = [:]
            }
            catalog = pendingCatalog
            catalogRevision = pendingRevision
            reads = reads.filter { catalog[$0.key] != nil }
            dismissed = dismissed.filter { catalog[$0.key] != nil }
        } else {
            guard response.offset == pageOffset, response.total == catalog.count,
                  Set(response.items.map(\.id)).count == response.items.count,
                  let bodies = try? decoder.decode(BodyResponse.self, from: data).items,
                  bodies.allSatisfy({ item in
                      catalog[item.id] == item.revision &&
                      (1...32_503_680_000).contains(item.updated_at) &&
                      item.title.utf8.count <= 240 && item.body.utf8.count <= 8000
                  }) else {
                failed(requestID: id)
                return
            }
            cancelRequest()
            pageIDs[response.offset] = bodies.map(\.id)
            pageBodies[response.offset] = bodies
        }
        state.failed = false
        rebuildVisibleItems()
    }

    private func cancelRequest() {
        timeout?.invalidate()
        timeout = nil
        requestID = nil
        state.loading = false
    }

    private func request(offset: Int) {
        guard connected, !scope.isEmpty else { return }
        let id = UUID().uuidString
        guard let data = try? JSONEncoder().encode(Request(
            request_id: id, summary_only: summary, offset: offset)) else { return }
        requestID = id
        state.loading = true
        state.failed = false
        let timer = Timer(timeInterval: 12, repeats: false) { [weak self] _ in
            self?.failed(requestID: id)
        }
        timeout = timer
        RunLoop.main.add(timer, forMode: .common)
        send?(data, id)
    }

    private func updateReadingState() {
        let visible = catalog.filter { dismissed[$0.key] != $0.value }
        state.total = visible.count
        state.unread = visible.filter { reads[$0.key] != $0.value }.count
        state.items = state.items.map { item in
            var item = item
            item.read = reads[item.id] == item.revision
            return item
        }
    }

    private func rebuildVisibleItems() {
        updateReadingState()
        let wanted = min(visibleLimit, state.total)
        var visible: [Announcement] = []
        var retainedPages: Set<Int> = []
        var position = 0
        var nextOffset: Int?
        summary = false
        if wanted > 0 {
            for offset in stride(from: 0, to: catalog.count, by: 20) {
                guard let ids = pageIDs[offset] else { nextOffset = offset; break }
                var selection: Set<Int64> = []
                for id in ids {
                    guard let revision = catalog[id], dismissed[id] != revision else { continue }
                    if position < wanted { selection.insert(id) }
                    position += 1
                }
                if selection.isEmpty { pageBodies[offset] = nil; continue }
                retainedPages.insert(offset)
                guard let bodies = pageBodies[offset] else { nextOffset = offset; break }
                visible += bodies.filter { selection.contains($0.id) }
                if visible.count == wanted { break }
            }
        }
        if visible.count == wanted {
            pageBodies = pageBodies.filter { retainedPages.contains($0.key) }
        } else {
            // Retain unchanged bodies while fetching a refreshed catalog.
            var retained = Set(visible.map(\.id))
            for item in state.items where catalog[item.id] == item.revision &&
                dismissed[item.id] != item.revision && retained.insert(item.id).inserted {
                visible.append(item)
                if visible.count == wanted { break }
            }
        }
        state.items = visible
        state.loaded = true
        updateReadingState()
        if let nextOffset {
            pageOffset = nextOffset
            request(offset: nextOffset)
        }
    }

    private func loadLocalState() {
        guard let file,
              let size = try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize,
              size <= 16 * 1024 * 1024,
              let data = try? Data(contentsOf: file),
              let saved = try? JSONDecoder().decode(LocalState.self, from: data),
              saved.scope == scope, [1, 2].contains(saved.version) else { return }
        func revisions(_ entries: [String: Int64]) -> [Int64: Int64] {
            var result: [Int64: Int64] = [:]
            for (key, revision) in entries {
                if let id = Int64(key), id > 0, revision > 0 { result[id] = revision }
            }
            return result
        }
        reads = revisions(saved.reads)
        dismissed = revisions(saved.dismissed ?? [:])
    }

    private func saveLocalState(reads: [Int64: Int64], dismissed: [Int64: Int64]) -> Bool {
        guard let file else { return false }
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            var directory = directory
            var values = URLResourceValues()
            values.isExcludedFromBackup = true
            try directory.setResourceValues(values)
            let saved = LocalState(version: 2, scope: scope,
                reads: Dictionary(uniqueKeysWithValues: reads.map { (String($0.key), $0.value) }),
                dismissed: Dictionary(uniqueKeysWithValues: dismissed.map { (String($0.key), $0.value) }))
            try JSONEncoder().encode(saved).write(to: file, options: .atomic)
            return true
        } catch {
            return false
        }
    }
}
