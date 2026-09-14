import AVFoundation
import CoreImage
import CoreVideo
import Foundation
import Security
import UIKit

enum MouseControlMode: String, CaseIterable, Identifiable {
    case relative
    case absolute

    private static let defaultsKey = "crossdesk.mobile.mouse-control-mode"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .absolute: return "绝对位置"
        case .relative: return "相对位置"
        }
    }

    var detail: String {
        switch self {
        case .absolute:
            return "触摸位置直接对应远端屏幕位置，适合快速定位。"
        case .relative:
            return "像触控板一样滑动光标，点击时操作当前光标位置。"
        }
    }

    static var saved: MouseControlMode {
        guard let rawValue = UserDefaults.standard.string(forKey: defaultsKey),
              let mode = MouseControlMode(rawValue: rawValue) else {
            return .relative
        }
        return mode
    }

    func save() {
        UserDefaults.standard.set(rawValue, forKey: Self.defaultsKey)
    }
}

enum VideoCodecMode: String, CaseIterable, Identifiable {
    case hardware
    case software

    private static let defaultsKey = "crossdesk.mobile.video-codec-mode"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .hardware: return "硬件"
        case .software: return "软件"
        }
    }

    var detail: String {
        switch self {
        case .hardware:
            return "使用 VideoToolbox，延迟和耗电更低，推荐日常使用。"
        case .software:
            return "使用 OpenH264 软件编解码，适合兼容性测试。"
        }
    }

    static var saved: VideoCodecMode {
        guard let rawValue = UserDefaults.standard.string(forKey: defaultsKey),
              let mode = VideoCodecMode(rawValue: rawValue) else {
            return .hardware
        }
        return mode
    }

    func save() {
        UserDefaults.standard.set(rawValue, forKey: Self.defaultsKey)
    }
}

enum VideoAdaptationPolicy: String, CaseIterable, Identifiable {
    case frameRatePriority
    case qualityPriority
    case balanced

    private static let defaultsKey = "crossdesk.mobile.video-adaptation-policy"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .frameRatePriority: return "帧率优先"
        case .qualityPriority: return "画质优先"
        case .balanced: return "平衡"
        }
    }

    var detail: String {
        switch self {
        case .frameRatePriority:
            return "优先保持画面流畅，带宽或编码压力较大时会更积极地降低分辨率。"
        case .qualityPriority:
            return "始终保持较高分辨率，带宽不足时通过降低帧率来保证画面清晰度。"
        case .balanced:
            return "兼顾清晰度和流畅度，只在持续压力下逐步降低分辨率。"
        }
    }

    var bridgeValue: CrossDeskVideoAdaptationPolicy {
        switch self {
        case .frameRatePriority: return .frameRatePriority
        case .qualityPriority: return .qualityPriority
        case .balanced: return .balanced
        }
    }

    static var saved: VideoAdaptationPolicy {
        guard let rawValue = UserDefaults.standard.string(forKey: defaultsKey),
              let policy = VideoAdaptationPolicy(rawValue: rawValue) else {
            return .qualityPriority
        }
        return policy
    }

    func save() {
        UserDefaults.standard.set(rawValue, forKey: Self.defaultsKey)
    }
}

private struct RemoteVideoFrame {
    let pixelBuffer: CVPixelBuffer
    let encodedSize: CGSize
}

struct RecentConnection: Codable, Identifiable, Equatable {
    let remoteID: String
    var displayName: String
    var lastConnectedAt: Date
    var remembersPassword: Bool
    var thumbnailFileName: String?

    var id: String { remoteID }
}

private enum RecentConnectionStore {
    private static let defaultsKey = "crossdesk.mobile.recent-connections.v1"
    private static let thumbnailQueue =
        DispatchQueue(label: "cn.crossdesk.mobile.thumbnails", qos: .utility)
    private static let context = CIContext(options: [.cacheIntermediates: false])

    static func load() -> [RecentConnection] {
        guard let data = UserDefaults.standard.data(forKey: defaultsKey),
              let connections = try? JSONDecoder().decode([RecentConnection].self,
                                                           from: data) else {
            return []
        }
        return connections.sorted { $0.lastConnectedAt > $1.lastConnectedAt }
    }

    static func save(_ connections: [RecentConnection]) {
        guard let data = try? JSONEncoder().encode(connections) else { return }
        UserDefaults.standard.set(data, forKey: defaultsKey)
    }

    static func thumbnailURL(fileName: String) -> URL? {
        guard let directory = thumbnailDirectory() else { return nil }
        return directory.appendingPathComponent(fileName, isDirectory: false)
    }

    static func captureThumbnail(from pixelBuffer: CVPixelBuffer,
                                 remoteID: String,
                                 completion: @escaping (String?) -> Void) {
        thumbnailQueue.async {
            let fileName: String? = autoreleasepool {
                let image = CIImage(cvPixelBuffer: pixelBuffer)
                let targetSize = CGSize(width: 640, height: 360)
                guard image.extent.width > 0, image.extent.height > 0 else {
                    return nil
                }

                // Scale in Core Image before materializing a CGImage. Creating
                // a full 4K bitmap and then drawing it into a thumbnail can add
                // tens of megabytes to the first-frame memory peak.
                let scale = max(targetSize.width / image.extent.width,
                                targetSize.height / image.extent.height)
                let scaled = image.transformed(by: CGAffineTransform(
                    scaleX: scale,
                    y: scale
                ))
                let cropRect = CGRect(
                    x: scaled.extent.midX - targetSize.width / 2,
                    y: scaled.extent.midY - targetSize.height / 2,
                    width: targetSize.width,
                    height: targetSize.height
                ).integral
                guard let thumbnailImage = context.createCGImage(
                    scaled.cropped(to: cropRect),
                    from: cropRect
                ) else {
                    return nil
                }

                let safeID = remoteID.filter {
                    $0.isLetter || $0.isNumber || $0 == "-"
                }
                let name = "\(safeID.isEmpty ? "remote" : safeID).jpg"
                guard let url = thumbnailURL(fileName: name),
                      let data = UIImage(cgImage: thumbnailImage)
                        .jpegData(compressionQuality: 0.78) else {
                    return nil
                }

                do {
                    try data.write(to: url, options: .atomic)
                    return name
                } catch {
                    return nil
                }
            }
            DispatchQueue.main.async { completion(fileName) }
        }
    }

    static func removeThumbnail(fileName: String?) {
        guard let fileName, let url = thumbnailURL(fileName: fileName) else { return }
        try? FileManager.default.removeItem(at: url)
    }

    private static func thumbnailDirectory() -> URL? {
        guard let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                                  in: .userDomainMask).first else {
            return nil
        }
        let directory = base.appendingPathComponent("RecentConnectionThumbnails",
                                                    isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: directory,
                                                    withIntermediateDirectories: true)
            return directory
        } catch {
            return nil
        }
    }
}

private enum ConnectionCredentialStore {
    private static let service = "cn.crossdesk.mobile.saved-password"

    static func password(for remoteID: String) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: remoteID,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]
        var result: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data else {
            return nil
        }
        return String(data: data, encoding: .utf8)
    }

    static func save(password: String, for remoteID: String) {
        removePassword(for: remoteID)
        let attributes: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: remoteID,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
            kSecValueData as String: Data(password.utf8)
        ]
        SecItemAdd(attributes as CFDictionary, nil)
    }

    static func removePassword(for remoteID: String) {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: remoteID
        ]
        SecItemDelete(query as CFDictionary)
    }
}

private final class RemoteAudioPlayer {
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private let queue = DispatchQueue(label: "cn.crossdesk.mobile.audio")
    private let format = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                       sampleRate: 48_000,
                                       channels: 1,
                                       interleaved: false)!
    private var enabled = true
    private var queuedBuffers = 0

    init() {
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
    }

    func setEnabled(_ value: Bool) {
        queue.async {
            self.enabled = value
            if value {
                self.startIfNeeded()
            } else {
                self.player.stop()
                self.engine.stop()
                self.queuedBuffers = 0
            }
        }
    }

    func enqueue(_ data: Data) {
        guard !data.isEmpty, data.count.isMultiple(of: MemoryLayout<Int16>.size) else { return }
        queue.async {
            guard self.enabled, self.queuedBuffers < 80 else { return }
            self.startIfNeeded()
            guard self.engine.isRunning,
                  let buffer = AVAudioPCMBuffer(pcmFormat: self.format,
                                                frameCapacity: AVAudioFrameCount(data.count / 2)),
                  let samples = buffer.int16ChannelData?[0] else { return }
            buffer.frameLength = buffer.frameCapacity
            data.withUnsafeBytes { bytes in
                guard let source = bytes.baseAddress else { return }
                memcpy(samples, source, data.count)
            }
            self.queuedBuffers += 1
            self.player.scheduleBuffer(buffer) {
                self.queue.async {
                    self.queuedBuffers = max(0, self.queuedBuffers - 1)
                }
            }
            if !self.player.isPlaying {
                self.player.play()
            }
        }
    }

    private func startIfNeeded() {
        guard enabled, !engine.isRunning else { return }
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .moviePlayback,
                                    options: [.mixWithOthers])
            try session.setActive(true)
            try engine.start()
            player.play()
        } catch {
            // The next PCM packet retries activation after route changes.
        }
    }
}

final class RemoteSessionModel: NSObject, ObservableObject, CrossDeskRTCBridgeDelegate {
    @Published var signalHost = "api.crossdesk.cn"
    @Published var signalPort = "9099"
    @Published var mouseControlMode = MouseControlMode.saved {
        didSet { mouseControlMode.save() }
    }
    @Published var videoCodecMode = VideoCodecMode.saved {
        didSet { videoCodecMode.save() }
    }
    @Published var videoAdaptationPolicy = VideoAdaptationPolicy.saved {
        didSet { videoAdaptationPolicy.save() }
    }
    @Published var remoteID = ""
    @Published var password = ""
    @Published private(set) var signalStatus = "正在连接信令服务"
    @Published private(set) var connectionStatus = "未连接"
    @Published private(set) var localIdentity = ""
    @Published private(set) var isConnecting = false
    @Published private(set) var isConnected = false
    @Published private(set) var sessionVisible = false
    @Published private var videoFrame: RemoteVideoFrame?
    @Published private(set) var remoteCursorVisible = true
    @Published private(set) var remoteCursorShape = 0
    @Published private(set) var remoteCursorPosition: CGPoint?
    @Published private(set) var remoteCursorPositionRevision: UInt64 = 0
    @Published private(set) var remoteCursorVisualOffset = CGPoint.zero
    @Published private(set) var hasRemoteCursorState = false
    @Published private(set) var bitrate: UInt = 0
    @Published private(set) var lossRate: Float = 0
    @Published private(set) var usingTURN = false
    @Published private(set) var displays: [String] = ["显示器 1"]
    @Published private(set) var displaySizes: [CGSize] = []
    @Published var selectedDisplay = 0
    @Published var audioEnabled = true
    @Published private(set) var clipboardStatus = ""
    @Published private(set) var transferStatus = ""
    @Published private(set) var transferProgress = 0.0
    @Published private(set) var receivedFileURL: URL?
    @Published private(set) var frameCount: UInt64 = 0
    @Published private(set) var recentConnections = RecentConnectionStore.load()
    @Published private(set) var recentConnectionPresence: [String: Bool] = [:]
    @Published private(set) var deviceOfflineAlertVisible = false
    @Published private(set) var connectionFailureMessage: String?

    let bridge = CrossDeskRTCBridge()
    private let audioPlayer = RemoteAudioPlayer()
    private var activeRemoteID = ""
    private var pendingRememberPassword = false
    private var connectionRecorded = false
    private var shouldCaptureThumbnail = false
    private var activeDisplayName = ""
    private var remoteCursorSequence: UInt32?
    private var videoWasBackgrounded = false
    private var videoRecoveryTask: Task<Void, Never>?
    private var pendingPresenceRemoteID: String?
    private var presenceProbeGeneration: UInt64 = 0
    private var signalConnected = false
    private var presenceIsForeground = false
    private var awaitingPresenceSnapshot = true
    private var presenceUpdatedAt: [String: ContinuousClock.Instant] = [:]
    private var lastPresenceRefreshAt: ContinuousClock.Instant?
    private var presenceTimer: Timer?
    private let presenceClock = ContinuousClock()
    private static let presenceRefreshInterval: Duration = .seconds(30)
    private static let presenceMaxAge: Duration = .seconds(60)

    private struct BridgeConfiguration: Equatable {
        let host: String
        let signalPort: Int
    }
    private var bridgeConfiguration: BridgeConfiguration?

    var pixelBuffer: CVPixelBuffer? { videoFrame?.pixelBuffer }
    var frameSize: CGSize { videoFrame?.encodedSize ?? .zero }
    var displayGeometrySize: CGSize {
        guard displaySizes.indices.contains(selectedDisplay) else {
            return frameSize
        }
        let size = displaySizes[selectedDisplay]
        return size.width > 0 && size.height > 0 ? size : frameSize
    }

    override init() {
        super.init()
        bridge.delegate = self
        configureBridge()
    }

    deinit {
        presenceTimer?.invalidate()
    }

    @discardableResult
    func configureBridge() -> Bool {
        let host = signalHost.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let signal = Int(signalPort),
              (1...65535).contains(signal),
              !host.isEmpty else {
            signalStatus = "服务器配置无效"
            return false
        }
        let configuration = BridgeConfiguration(host: host, signalPort: signal)
        if bridgeConfiguration != configuration {
            bridgeConfiguration = configuration
            signalConnected = false
            signalStatus = "正在连接信令服务"
            stopPresenceMonitoring()
            invalidatePresence()
            cancelPendingPresenceConnection()
        } else if signalConnected {
            signalStatus = "已连接服务器"
        }
        bridge.setHardwareAccelerationEnabled(videoCodecMode == .hardware)
        bridge.setVideoAdaptationPolicy(videoAdaptationPolicy.bridgeValue)
        bridge.configure(withSignalHost: host,
                         signalPort: signal)
        return true
    }

    func connect(password: String, rememberPassword: Bool) {
        self.password = password
        pendingRememberPassword = rememberPassword
        connect()
    }

    func connect() {
        connectionFailureMessage = nil
        let identifier = remoteID.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !identifier.isEmpty else {
            connectionStatus = "请输入远程设备 ID"
            return
        }
        remoteID = identifier
        guard configureBridge() else {
            connectionStatus = signalStatus
            return
        }
        if isDeviceOnline(identifier) {
            beginRemoteConnection(identifier)
        } else {
            beginPresenceProbe(identifier)
        }
    }

    private func beginRemoteConnection(_ identifier: String) {
        cancelVideoRecovery()
        cancelPresenceProbe()
        activeRemoteID = identifier
        activeDisplayName = ""
        displaySizes = []
        connectionRecorded = false
        shouldCaptureThumbnail = true
        isConnecting = true
        isConnected = false
        sessionVisible = false
        resetRemoteCursorState()
        connectionStatus = "正在准备连接…"
        bridge.connect(toRemoteID: identifier, password: password)
    }

    private func beginPresenceProbe(_ identifier: String) {
        guard signalConnected, presenceIsForeground else {
            showDeviceOffline()
            return
        }

        presenceProbeGeneration &+= 1
        let generation = presenceProbeGeneration
        pendingPresenceRemoteID = identifier
        isConnecting = true
        isConnected = false
        sessionVisible = false
        connectionStatus = "正在确认设备状态…"
        requestPresenceProbe(identifier)

        DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in
            guard let self,
                  self.presenceProbeGeneration == generation,
                  self.pendingPresenceRemoteID == identifier else {
                return
            }
            self.cancelPresenceProbe()
            self.refreshRecentConnectionPresence()
            self.showDeviceOffline()
        }
    }

    private func cancelPresenceProbe() {
        presenceProbeGeneration &+= 1
        pendingPresenceRemoteID = nil
    }

    private func cancelPendingPresenceConnection() {
        guard pendingPresenceRemoteID != nil else { return }
        cancelPresenceProbe()
        isConnecting = false
        connectionStatus = "未连接"
    }

    private func requestPresenceProbe(_ identifier: String) {
        guard signalConnected, presenceIsForeground else { return }
        // Older servers ignore subscribe=false and replace the watched list.
        var identifiers = recentConnections.map(\.remoteID)
        if !identifiers.contains(identifier) { identifiers.append(identifier) }
        bridge.requestPresence(remoteIDs: identifiers, subscribe: false)
    }

    private func showDeviceOffline() {
        isConnecting = false
        isConnected = false
        sessionVisible = false
        connectionStatus = "设备离线"
        deviceOfflineAlertVisible = true
    }

    func dismissDeviceOfflineAlert() {
        deviceOfflineAlertVisible = false
    }

    func dismissConnectionFailure() {
        guard connectionFailureMessage != nil else { return }
        connectionFailureMessage = nil
    }

    private func finishConnection(withError message: String) {
        // Cleanup may emit Closed. Preserve the terminal reason independently
        // of the progress view and invalidate those callbacks in the bridge.
        resetConnection()
        connectionStatus = message
        connectionFailureMessage = message
    }

    func disconnect() {
        resetConnection()
    }

    private func resetConnection() {
        connectionFailureMessage = nil
        cancelVideoRecovery()
        let wasCheckingPresence = pendingPresenceRemoteID != nil
        cancelPresenceProbe()
        if wasCheckingPresence {
            refreshRecentConnectionPresence()
        }
        bridge.disconnect()
        isConnecting = false
        isConnected = false
        sessionVisible = false
        videoFrame = nil
        resetRemoteCursorState()
        frameCount = 0
        bitrate = 0
        displays = ["显示器 1"]
        displaySizes = []
        selectedDisplay = 0
        connectionStatus = "未连接"
        audioPlayer.setEnabled(false)
        AppOrientation.update(to: .portrait)
    }

    func retry() {
        cancelVideoRecovery()
        bridge.disconnect()
        connect()
    }

    func savedPassword(for remoteID: String) -> String {
        ConnectionCredentialStore.password(for: remoteID) ?? ""
    }

    func savedCredential(for remoteID: String) -> String? {
        ConnectionCredentialStore.password(for: remoteID)
    }

    func remembersPassword(for remoteID: String) -> Bool {
        recentConnections.first(where: { $0.remoteID == remoteID })?
            .remembersPassword == true
    }

    func thumbnailImage(for connection: RecentConnection) -> UIImage? {
        guard let fileName = connection.thumbnailFileName,
              let url = RecentConnectionStore.thumbnailURL(fileName: fileName) else {
            return nil
        }
        return UIImage(contentsOfFile: url.path)
    }

    func isRecentConnectionOnline(_ connection: RecentConnection) -> Bool {
        isDeviceOnline(connection.remoteID)
    }

    private func isDeviceOnline(_ identifier: String) -> Bool {
        guard signalConnected, presenceIsForeground, !awaitingPresenceSnapshot,
              recentConnectionPresence[identifier] == true,
              let updatedAt = presenceUpdatedAt[identifier] else { return false }
        return updatedAt.duration(to: presenceClock.now) < Self.presenceMaxAge
    }

    private func refreshRecentConnectionPresence() {
        guard signalConnected, presenceIsForeground else { return }
        lastPresenceRefreshAt = presenceClock.now
        bridge.requestPresence(remoteIDs: recentConnections.map(\.remoteID),
                               subscribe: true)
    }

    private func invalidatePresence() {
        bridge.invalidatePresence()
        recentConnectionPresence = [:]
        presenceUpdatedAt = [:]
        awaitingPresenceSnapshot = true
        lastPresenceRefreshAt = nil
    }

    private func startPresenceMonitoring() {
        guard signalConnected, presenceIsForeground, presenceTimer == nil else { return }
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            self?.maintainPresence()
        }
        RunLoop.main.add(timer, forMode: .common)
        presenceTimer = timer
    }

    private func stopPresenceMonitoring() {
        presenceTimer?.invalidate()
        presenceTimer = nil
    }

    private func maintainPresence() {
        guard signalConnected, presenceIsForeground else { return }
        let now = presenceClock.now
        let expired = presenceUpdatedAt.compactMap { identifier, updatedAt in
            updatedAt.duration(to: now) >= Self.presenceMaxAge ? identifier : nil
        }
        if !expired.isEmpty {
            var updated = recentConnectionPresence
            for identifier in expired {
                updated.removeValue(forKey: identifier)
                presenceUpdatedAt.removeValue(forKey: identifier)
            }
            recentConnectionPresence = updated
        }
        if lastPresenceRefreshAt.map({ $0.duration(to: now) >= Self.presenceRefreshInterval }) ?? true {
            refreshRecentConnectionPresence()
        }
    }

    func suspendPresenceMonitoring() {
        presenceIsForeground = false
        stopPresenceMonitoring()
        invalidatePresence()
        cancelPendingPresenceConnection()
    }

    func suspendVideoForBackground() {
        videoWasBackgrounded = true
        cancelVideoRecovery()
    }

    func resumeVideoAfterForeground() {
        guard videoWasBackgrounded else { return }
        videoWasBackgrounded = false
        guard isConnected, sessionVisible else { return }

        cancelVideoRecovery()
        NSLog("CrossDesk resuming foreground video, requesting key frame")
        bridge.requestKeyFrame()
        // The first request can race transport resumption. Retry briefly until
        // a decoded frame arrives, and never carry retries into another session.
        videoRecoveryTask = Task { @MainActor [weak self] in
            for delay in [500_000_000, 1_000_000_000, 2_000_000_000] as [UInt64] {
                do {
                    try await Task.sleep(nanoseconds: delay)
                } catch {
                    return
                }
                guard !Task.isCancelled, let self,
                      !self.videoWasBackgrounded,
                      self.isConnected, self.sessionVisible else { return }
                self.bridge.requestKeyFrame()
            }
        }
    }

    private func cancelVideoRecovery() {
        videoRecoveryTask?.cancel()
        videoRecoveryTask = nil
    }

    func refreshRecentConnectionPresenceAfterForeground() {
        presenceIsForeground = true
        invalidatePresence()
        startPresenceMonitoring()
        refreshRecentConnectionPresence()
        if let pendingPresenceRemoteID {
            requestPresenceProbe(pendingPresenceRemoteID)
        }
    }

    func removeRecentConnection(_ connection: RecentConnection) {
        recentConnections.removeAll { $0.remoteID == connection.remoteID }
        recentConnectionPresence.removeValue(forKey: connection.remoteID)
        presenceUpdatedAt.removeValue(forKey: connection.remoteID)
        RecentConnectionStore.save(recentConnections)
        RecentConnectionStore.removeThumbnail(fileName: connection.thumbnailFileName)
        ConnectionCredentialStore.removePassword(for: connection.remoteID)
        refreshRecentConnectionPresence()
    }

    private func recordSuccessfulConnectionIfNeeded() {
        guard !connectionRecorded, !activeRemoteID.isEmpty else { return }
        connectionRecorded = true

        if pendingRememberPassword {
            ConnectionCredentialStore.save(password: password, for: activeRemoteID)
        } else {
            ConnectionCredentialStore.removePassword(for: activeRemoteID)
        }

        let previous = recentConnections.first { $0.remoteID == activeRemoteID }
        let title = activeDisplayName.isEmpty
            ? (previous?.displayName ?? activeRemoteID)
            : activeDisplayName
        let connection = RecentConnection(
            remoteID: activeRemoteID,
            displayName: title,
            lastConnectedAt: Date(),
            remembersPassword: pendingRememberPassword,
            thumbnailFileName: previous?.thumbnailFileName
        )
        recentConnections.removeAll { $0.remoteID == activeRemoteID }
        recentConnections.insert(connection, at: 0)
        RecentConnectionStore.save(recentConnections)
        refreshRecentConnectionPresence()
    }

    private func updateActiveConnectionName(_ name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, !activeRemoteID.isEmpty else { return }
        activeDisplayName = trimmed
        guard let index = recentConnections.firstIndex(where: {
            $0.remoteID == activeRemoteID
        }) else { return }
        recentConnections[index].displayName = trimmed
        RecentConnectionStore.save(recentConnections)
    }

    private func captureRecentThumbnailIfNeeded(_ pixelBuffer: CVPixelBuffer) {
        guard shouldCaptureThumbnail, !activeRemoteID.isEmpty else { return }
        shouldCaptureThumbnail = false
        let identifier = activeRemoteID
        RecentConnectionStore.captureThumbnail(from: pixelBuffer,
                                                remoteID: identifier) { [weak self] fileName in
            guard let self, let fileName,
                  let index = self.recentConnections.firstIndex(where: {
                      $0.remoteID == identifier
                  }) else { return }
            self.recentConnections[index].thumbnailFileName = fileName
            RecentConnectionStore.save(self.recentConnections)
        }
    }

    func selectDisplay(_ index: Int) {
        guard displays.indices.contains(index) else { return }
        selectedDisplay = index
        videoFrame = nil
        resetRemoteCursorState()
        frameCount = 0
        bridge.switch(toDisplay: index)
    }

    private func resetRemoteCursorState() {
        remoteCursorVisible = true
        remoteCursorShape = 0
        remoteCursorPosition = nil
        remoteCursorPositionRevision &+= 1
        remoteCursorVisualOffset = .zero
        hasRemoteCursorState = false
        remoteCursorSequence = nil
    }

    func toggleAudio() {
        audioEnabled.toggle()
        audioPlayer.setEnabled(audioEnabled)
        bridge.setAudioEnabled(audioEnabled)
    }

    func sendClipboard() {
        guard let text = UIPasteboard.general.string, !text.isEmpty else {
            clipboardStatus = "剪贴板中没有文本"
            return
        }
        guard text.lengthOfBytes(using: .utf8) <= 128 * 1024 else {
            clipboardStatus = "剪贴板文本超过 128 KiB"
            return
        }
        bridge.sendClipboardText(text)
        clipboardStatus = "已发送本机剪贴板"
    }

    func sendFile(_ url: URL) {
        transferStatus = "正在发送 \(url.lastPathComponent)"
        transferProgress = 0
        bridge.sendFile(at: url)
    }

    func sendKeyStroke(_ keyCode: UInt) {
        bridge.sendWindowsKeyCode(keyCode, isDown: true)
        bridge.sendWindowsKeyCode(keyCode, isDown: false)
    }

    func sendKeyState(_ keyCode: UInt, isDown: Bool) {
        bridge.sendWindowsKeyCode(keyCode, isDown: isDown)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didChange state: CrossDeskSignalState) {
        signalConnected = state.rawValue == 1
        invalidatePresence()
        switch state.rawValue {
        case 0: signalStatus = "正在连接信令服务"
        case 1: signalStatus = "已连接服务器"
        case 2: signalStatus = "信令连接失败"
        case 3: signalStatus = "信令连接已关闭"
        case 4: signalStatus = "信令服务重连中"
        case 5: signalStatus = "信令服务器已关闭连接"
        case 6: signalStatus = "TLS 证书校验失败"
        default: signalStatus = "未知信令状态"
        }
        if signalConnected {
            startPresenceMonitoring()
            refreshRecentConnectionPresence()
            if let pendingPresenceRemoteID {
                requestPresenceProbe(pendingPresenceRemoteID)
            }
        } else {
            stopPresenceMonitoring()
            cancelPendingPresenceConnection()
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didChange state: CrossDeskConnectionState,
                   remoteID: String) {
        guard isConnecting || isConnected || sessionVisible,
              remoteID.isEmpty || remoteID == activeRemoteID else {
            return
        }
        isConnected = state.rawValue == 1
        if !isConnected {
            cancelVideoRecovery()
        }
        isConnecting = [0, 2].contains(state.rawValue)
        switch state.rawValue {
        case 0: connectionStatus = "正在建立远程连接"
        case 1:
            connectionStatus = "已连接"
            sessionVisible = true
            recordSuccessfulConnectionIfNeeded()
            audioPlayer.setEnabled(audioEnabled)
            bridge.setAudioEnabled(audioEnabled)
            AppOrientation.update(to: .allButUpsideDown)
        case 2: connectionStatus = "正在收集 ICE 候选"
        case 3: finishConnection(withError: "网络连接已中断，请重新连接。")
        case 4:
            finishConnection(withError: "P2P/TURN 建链失败，请检查网络后重试。")
        case 5:
            finishConnection(withError: "远程连接已结束。")
        case 6:
            finishConnection(withError: "访问密码错误，请重新输入。")
        case 7:
            if !remoteID.isEmpty {
                recentConnectionPresence[remoteID] = false
                presenceUpdatedAt.removeValue(forKey: remoteID)
            }
            finishConnection(withError: "远程设备 ID 不存在，请检查设备 ID。")
        case 8:
            if !remoteID.isEmpty {
                recentConnectionPresence[remoteID] = false
                presenceUpdatedAt.removeValue(forKey: remoteID)
            }
            finishConnection(withError: "远程设备当前不可用，请稍后重试。")
        default: connectionStatus = "未知连接状态"
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didProvisionIdentity identity: String) {
        localIdentity = identity.split(separator: "@").first.map(String.init) ?? identity
        refreshRecentConnectionPresence()
        if let pendingPresenceRemoteID {
            requestPresenceProbe(pendingPresenceRemoteID)
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didReceivePresence presence: [String: NSNumber],
                   snapshot: Bool) {
        guard signalConnected, presenceIsForeground,
              snapshot || !awaitingPresenceSnapshot else { return }
        var updated = snapshot ? [:] : recentConnectionPresence
        if snapshot {
            presenceUpdatedAt = [:]
            awaitingPresenceSnapshot = false
        }
        let now = presenceClock.now
        let watched = Set(recentConnections.map(\.remoteID))
        for (remoteID, online) in presence
            where watched.contains(remoteID) || pendingPresenceRemoteID == remoteID {
            updated[remoteID] = online.boolValue
            presenceUpdatedAt[remoteID] = now
        }
        recentConnectionPresence = updated

        guard let pendingRemoteID = pendingPresenceRemoteID,
              let online = presence[pendingRemoteID]?.boolValue else {
            return
        }
        cancelPresenceProbe()
        refreshRecentConnectionPresence()
        if online {
            beginRemoteConnection(pendingRemoteID)
        } else {
            showDeviceOffline()
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didReceive pixelBuffer: CVPixelBuffer,
                   width: Int,
                   height: Int) {
        guard !videoWasBackgrounded else { return }
        cancelVideoRecovery()
        // Buffer and encoded dimensions must be one observable value. Adaptive
        // resolution changes must never expose a new frame with the previous
        // frame's geometry to SwiftUI.
        videoFrame = RemoteVideoFrame(
            pixelBuffer: pixelBuffer,
            encodedSize: CGSize(width: width, height: height)
        )
        frameCount &+= 1
        captureRecentThumbnailIfNeeded(pixelBuffer)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didReceiveHostName hostName: String,
                   displayNames remoteDisplayNames: [String],
                   displaySizes remoteDisplaySizes: [NSValue]) {
        updateActiveConnectionName(hostName)
        let names = remoteDisplayNames.enumerated().map { index, displayName in
            let name = displayName.trimmingCharacters(in: .whitespacesAndNewlines)
            return name.isEmpty ? "显示器 \(index + 1)" : name
        }
        if !names.isEmpty {
            displays = names
            displaySizes = remoteDisplaySizes.map(\.cgSizeValue)
            selectedDisplay = min(selectedDisplay, names.count - 1)
            bridge.switch(toDisplay: selectedDisplay)
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didReceiveCursorVisible visible: Bool,
                   shape: Int,
                   positionUpdate: Bool,
                   positionValid: Bool,
                   x: Float,
                   y: Float,
                   visualOffsetX: Float,
                   visualOffsetY: Float,
                   display displayIndex: Int,
                   sequence: UInt32) {
        if let previous = remoteCursorSequence,
           sequence != 0,
           Int32(bitPattern: sequence &- previous) <= 0 {
            return
        }

        remoteCursorSequence = sequence
        remoteCursorVisible = visible
        remoteCursorShape = min(max(shape, 0), 28)
        remoteCursorVisualOffset = CGPoint(x: CGFloat(visualOffsetX),
                                           y: CGFloat(visualOffsetY))
        if positionUpdate {
            if positionValid, displayIndex == selectedDisplay {
                remoteCursorPosition = CGPoint(
                    x: CGFloat(min(max(x, 0), 1)),
                    y: CGFloat(min(max(y, 0), 1))
                )
            } else {
                remoteCursorPosition = nil
            }
            remoteCursorPositionRevision &+= 1
        }
        hasRemoteCursorState = true
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveAudioPCM pcmData: Data) {
        audioPlayer.enqueue(pcmData)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveClipboardText text: String) {
        UIPasteboard.general.string = text
        clipboardStatus = "已接收远端剪贴板"
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didUpdateFileTransfer fileName: String,
                   progress: Double,
                   sending: Bool) {
        if progress < 0 {
            transferStatus = "\(fileName) 传输失败"
            transferProgress = 0
            return
        }
        transferProgress = progress
        let percent = Int((progress * 100).rounded())
        transferStatus = "\(sending ? "发送" : "接收") \(fileName) · \(percent)%"
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveFileAt fileURL: URL) {
        receivedFileURL = fileURL
        transferStatus = "已接收 \(fileURL.lastPathComponent)"
        transferProgress = 1
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didUpdateBitrate bitsPerSecond: UInt,
                   lossRate: Float,
                   usingTURN: Bool) {
        bitrate = bitsPerSecond
        self.lossRate = lossRate
        self.usingTURN = usingTURN
    }

    var formattedBitrate: String {
        if bitrate >= 1_000_000 {
            return String(format: "%.1f Mbps", Double(bitrate) / 1_000_000)
        }
        if bitrate >= 1_000 {
            return String(format: "%.0f Kbps", Double(bitrate) / 1_000)
        }
        return "\(bitrate) bps"
    }

    var videoStatus: String {
        if frameCount > 0 {
            return "视频 \(Int(frameSize.width))×\(Int(frameSize.height)) · \(frameCount) 帧"
        }
        return isConnected ? "正在取回远程画面…" : "等待连接"
    }
}
