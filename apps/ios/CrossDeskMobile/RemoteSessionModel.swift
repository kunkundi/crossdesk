import AVFoundation
import CoreImage
import CoreVideo
import Foundation
import Network
import Security
import UIKit

enum MouseControlMode: String, CaseIterable, Identifiable {
    case relative
    case absolute

    private static let defaultsKey = "crossdesk.mobile.mouse-control-mode"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .absolute: return NSLocalizedString("绝对位置", comment: "")
        case .relative: return NSLocalizedString("相对位置", comment: "")
        }
    }

    var detail: String {
        switch self {
        case .absolute:
            return NSLocalizedString("触摸位置直接对应远端屏幕位置，适合快速定位。", comment: "")
        case .relative:
            return NSLocalizedString("像触控板一样滑动光标，点击时操作当前光标位置。", comment: "")
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

enum VideoAdaptationPolicy: String, CaseIterable, Identifiable {
    case frameRatePriority
    case qualityPriority
    case balanced

    private static let defaultsKey = "crossdesk.mobile.video-adaptation-policy"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .frameRatePriority: return NSLocalizedString("帧率优先", comment: "")
        case .qualityPriority: return NSLocalizedString("画质优先", comment: "")
        case .balanced: return NSLocalizedString("平衡", comment: "")
        }
    }

    var detail: String {
        switch self {
        case .frameRatePriority:
            return NSLocalizedString("优先保持画面流畅，带宽或编码压力较大时会更积极地降低分辨率。", comment: "")
        case .qualityPriority:
            return NSLocalizedString("始终保持较高分辨率，带宽不足时通过降低帧率来保证画面清晰度。", comment: "")
        case .balanced:
            return NSLocalizedString("兼顾清晰度和流畅度，只在持续压力下逐步降低分辨率。", comment: "")
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
    let id: UInt64
    let captureUptime: TimeInterval
}

private enum RecentConnectionStore {
    private static let thumbnails = ThumbnailFileStore(directory:
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("RecentConnectionThumbnails", isDirectory: true))
    private static let context = CIContext(options: [.cacheIntermediates: false])

    static func thumbnailURL(fileName: String) -> URL {
        thumbnails.url(for: fileName)
    }

    static func captureThumbnail(from pixelBuffer: CVPixelBuffer, name: String,
                                 completion: @escaping (String?) -> Void) {
        thumbnails.save(fileName: name, makeData: {
            let image = CIImage(cvPixelBuffer: pixelBuffer)
            let targetSize = CGSize(width: 640, height: 360)
            guard image.extent.width > 0, image.extent.height > 0 else { return nil }
            // Scale before materializing a bitmap to avoid a full-size frame copy.
            let scale = max(targetSize.width / image.extent.width,
                            targetSize.height / image.extent.height)
            let scaled = image.transformed(by: CGAffineTransform(scaleX: scale, y: scale))
            let cropRect = CGRect(x: scaled.extent.midX - targetSize.width / 2,
                                  y: scaled.extent.midY - targetSize.height / 2,
                                  width: targetSize.width, height: targetSize.height).integral
            guard let thumbnailImage = context.createCGImage(scaled.cropped(to: cropRect),
                                                              from: cropRect) else { return nil }
            return UIImage(cgImage: thumbnailImage).jpegData(compressionQuality: 0.78)
        }) { name in
            DispatchQueue.main.async { completion(name) }
        }
    }

    static func removeLegacyThumbnails() {
        thumbnails.removeLegacyFiles()
    }

    static func removeThumbnail(named name: String) {
        thumbnails.remove(fileName: name)
    }

    static func removeAllThumbnails(completion: @escaping (Bool) -> Void) {
        thumbnails.removeAll { success in
            DispatchQueue.main.async { completion(success) }
        }
    }

}

private enum ConnectionCredentialStore {
    private static let service = "cn.crossdesk.mobile.saved-password"

    static func password(for account: String) -> String? {
        var attributes = query(for: account)
        attributes[kSecReturnData as String] = true
        attributes[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        guard SecItemCopyMatching(attributes as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    private static func query(for account: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: account]
    }

    @discardableResult
    static func save(password: String, for account: String) -> Bool {
        let query = query(for: account)
        let value = [kSecValueData as String: Data(password.utf8)]
        let status = SecItemUpdate(query as CFDictionary, value as CFDictionary)
        if status != errSecItemNotFound { return status == errSecSuccess }
        var attributes = query.merging(value) { _, new in new }
        attributes[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        return SecItemAdd(attributes as CFDictionary, nil) == errSecSuccess
    }

    @discardableResult
    static func removePassword(for account: String) -> Bool {
        let status = SecItemDelete(query(for: account) as CFDictionary)
        return status == errSecSuccess || status == errSecItemNotFound
    }

}

private final class RemoteAudioPlayer {
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private let queue = DispatchQueue(label: "cn.crossdesk.mobile.audio")
    // Older iOS audio units can reject an Int16 player-to-mixer connection
    // with an Objective-C exception. Use the standard Float32 graph format.
    private let format = AVAudioFormat(standardFormatWithSampleRate: 48_000,
                                       channels: 1)!
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

    func endSession() {
        queue.async {
            self.enabled = false
            self.player.stop()
            self.engine.stop()
            self.queuedBuffers = 0
            try? AVAudioSession.sharedInstance().setActive(false, options: [.notifyOthersOnDeactivation])
        }
    }

    func enqueue(_ data: Data) {
        guard !data.isEmpty, data.count.isMultiple(of: MemoryLayout<Int16>.size),
              let frameCount = AVAudioFrameCount(exactly: data.count / MemoryLayout<Int16>.size) else { return }
        queue.async {
            guard self.enabled, self.queuedBuffers < 80 else { return }
            self.startIfNeeded()
            guard self.engine.isRunning,
                  let buffer = AVAudioPCMBuffer(pcmFormat: self.format,
                                                frameCapacity: frameCount),
                  let samples = buffer.floatChannelData?[0] else { return }
            buffer.frameLength = buffer.frameCapacity
            data.withUnsafeBytes { bytes in
                // MiniRTC delivers native-endian signed 16-bit PCM. Data's
                // storage may be unaligned, including for short inline values.
                for index in 0..<Int(frameCount) {
                    let sample = bytes.loadUnaligned(
                        fromByteOffset: index * MemoryLayout<Int16>.size, as: Int16.self)
                    samples[index] = Float(sample) / 32_768.0
                }
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

struct SignalServerConfiguration: Equatable {
    let host: String
    let signalPort: Int

    var historyEndpoint: String { CrossDeskRTCBridge.historyEndpoint(host, port: signalPort) }
    var history: ConnectionHistory { ConnectionHistory(endpoint: historyEndpoint) }

    static let official = Self(host: "api.crossdesk.cn", signalPort: 9099)
    private static let customServerKey = "crossdesk.mobile.custom-signal-server"

    static func loadCustom() -> Self? {
        guard let saved = UserDefaults.standard.dictionary(forKey: customServerKey),
              let host = (saved["host"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines),
              !host.isEmpty,
              let port = saved["port"] as? Int, (1...65535).contains(port) else { return nil }
        return Self(host: host, signalPort: port)
    }

    static func saveCustom(_ server: Self?) {
        if let server {
            UserDefaults.standard.set(["host": server.host, "port": server.signalPort], forKey: customServerKey)
        } else {
            UserDefaults.standard.removeObject(forKey: customServerKey)
        }
    }
}

final class RemoteSessionModel: NSObject, ObservableObject, CrossDeskRTCBridgeDelegate {
    private let privacyPreferences = PrivacyPreferences()
    @Published private(set) var hasNetworkConsent = PrivacyPreferences().hasNetworkConsent
    @Published private var privacyNotice = PrivacyNoticeState(
        hasNetworkConsent: PrivacyPreferences().hasNetworkConsent
    )
    @Published private(set) var savesConnectionThumbnails = PrivacyPreferences().savesThumbnails
    @Published private(set) var thumbnailCleanupInProgress = false
    @Published private(set) var thumbnailCleanupError: String?
    private var thumbnailGeneration = UUID()

    @Published private(set) var customServer = SignalServerConfiguration.loadCustom()
    @Published var mouseControlMode = MouseControlMode.saved {
        didSet { mouseControlMode.save() }
    }
    @Published var videoAdaptationPolicy = VideoAdaptationPolicy.saved {
        didSet { videoAdaptationPolicy.save() }
    }
    @Published var autoLandscape = UserDefaults.standard.object(forKey: AppOrientation.autoLandscapeKey) as? Bool ?? false {
        didSet { UserDefaults.standard.set(autoLandscape, forKey: AppOrientation.autoLandscapeKey) }
    }
    @Published private(set) var videoSettings = RemoteVideoSettingsState()
    @Published private(set) var displayResolutions = RemoteDisplayResolutionState()
    @Published var privacyScreen = RemotePrivacyScreen()
    @Published private(set) var privacyClock = ProcessInfo.processInfo.systemUptime
    private var privacyTimer: Timer?
    @Published var remoteID = ""
    @Published var password = ""
    @Published private(set) var signalStatus = NSLocalizedString("等待隐私授权", comment: "")
    @Published private(set) var connectionStatus = NSLocalizedString("未连接", comment: "")
    @Published private(set) var localIdentity = ""
    @Published private(set) var identityStorageWarning: String?
    @Published private(set) var isConnecting = false
    @Published private(set) var isConnected = false
    @Published private(set) var sessionVisible = false
    var canStartConnection: Bool { !isConnecting && !isConnected && !sessionVisible }
    @Published private var videoFrame: RemoteVideoFrame?
    @Published private(set) var remoteCursorVisible = true
    @Published private(set) var remoteCursorShape = 0
    @Published private(set) var remoteCursorPosition: CGPoint?
    @Published private(set) var remoteCursorPositionRevision: UInt64 = 0
    @Published private(set) var remoteCursorVisualOffset = CGPoint.zero
    @Published private(set) var hasRemoteCursorState = false
    private var networkStatistics = RemoteNetworkStatistics()
    private var videoFrameID: UInt64 = 0
    @Published private(set) var displays: [String] = [NSLocalizedString("显示器 1", comment: "")]
    @Published private(set) var displaySizes: [CGSize] = []
    @Published var selectedDisplay = 0
    @Published var audioEnabled = true
    @Published private(set) var clipboardStatus = ""
    @Published private(set) var keyboardInputStatus = ""
    @Published private(set) var supportsTextInput = false
    @Published private(set) var controlStatus = ""
    @Published private(set) var transferStatus = ""
    @Published private(set) var transferProgress = 0.0
    @Published private(set) var receivedFileURL: URL?
    @Published private(set) var frameCount: UInt64 = 0
    @Published private(set) var history = SignalServerConfiguration.official.history
    @Published private(set) var recentConnections: [RecentConnection] = []
    private var activeHistory: ConnectionHistory?
    private var pendingCaptures: [String: UUID] = [:]
    @Published private(set) var recentConnectionPresence: [String: Bool] = [:]
    @Published private(set) var deviceOfflineAlertVisible = false
    @Published private(set) var connectionFailureMessage: String?
    @Published private(set) var remoteUpdateMessage: String?
    @Published private(set) var remotePlatform: RemoteHostPlatform = .unknown

    let bridge = CrossDeskRTCBridge()
    private let networkPath = NWPathMonitor()
    let pictureInPicture = RemotePictureInPicture()
    let announcements = AnnouncementInbox()
    let appUpdates = AppUpdateChecker()
    private var appIsActive = false
    private lazy var audioPlayer = RemoteAudioPlayer()
    private var activeRemoteID = ""
    private var resolutionPreferenceKey = ""
    private var remoteAppVersion = ""
    private var remoteHostInfoReceived = false
    private var remoteVersionCheckAttempted = false
    private var remoteVersionCheckGeneration = UUID()
    private var remoteVersionCheckTask: URLSessionDataTask?
    private static let remoteUpdateNotice = NSLocalizedString("被控端版本过低，请升级到最新版本。", comment: "")
    private var pendingRememberPassword = false
    private var expectedHostKey = ""
    private var verifiedHostKey = ""
    private var usingSavedCredential = false
    struct HostKeyChange: Equatable {
        let target: String
        let expected: String
        let observed: String
    }
    @Published var hostKeyChange: HostKeyChange?
    struct DirectPasswordPrompt: Equatable {
        let target: String
        let expectedKey: String
    }
    @Published var requestedPasswordPrompt: DirectPasswordPrompt?
    @Published var rejectedPasswordPrompt: DirectPasswordPrompt?
    private var rejectedCredentialAccount: String?
    private var connectionRecorded = false
    private var shouldCaptureThumbnail = false
    private var activeDisplayName = ""
    private var remoteCursorSequence: UInt32?
    private var remoteServiceAvailable = false
    private var videoWasBackgrounded = false
    private var videoRecoveryTask: Task<Void, Never>?
    private var videoSettingsTimeoutTask: Task<Void, Never>?
    private var displayResolutionTimeoutTask: Task<Void, Never>?
    private var pendingPresenceRemoteID: String?
    private var presenceProbeGeneration: UInt64 = 0
    @Published private(set) var signalConnected = false
    @Published private(set) var signalHasError = false
    private var presenceIsForeground = false
    private var awaitingPresenceSnapshot = true
    private var presenceUpdatedAt: [String: ContinuousClock.Instant] = [:]
    private var lastPresenceRefreshAt: ContinuousClock.Instant?
    private var presenceTimer: Timer?
    private let presenceClock = ContinuousClock()
    private static let presenceRefreshInterval: Duration = .seconds(30)
    private static let presenceMaxAge: Duration = .seconds(60)

    private var bridgeConfiguration: SignalServerConfiguration?

    var pixelBuffer: CVPixelBuffer? { videoFrame?.pixelBuffer }
    var currentVideoFrameID: UInt64 { videoFrame?.id ?? 0 }
    var frameCaptureUptime: TimeInterval { videoFrame?.captureUptime ?? 0 }
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
        history = (customServer ?? .official).history
        history.adoptLegacyHistory()
        recentConnections = history.load()
        RecentConnectionStore.removeLegacyThumbnails()
        bridge.delegate = self
        networkPath.pathUpdateHandler = { [weak self] path in
            guard path.status == .satisfied else { return }
            DispatchQueue.main.async { [weak self] in
                guard let self, self.hasNetworkConsent, self.appIsActive else { return }
                self.bridge.verifySignaling()
            }
        }
        networkPath.start(queue: DispatchQueue(label: "crossdesk.signaling.path"))
        pictureInPicture.requestFrame = { [weak self] in
            guard let self, self.isConnected else { return }
            self.bridge.requestKeyFrame()
        }
        pictureInPicture.restoreInterface = { [weak self] in
            guard let self, self.isConnected else { return false }
            self.sessionVisible = true
            return true
        }
        pictureInPicture.playbackChanged = { [weak self] playing in
            guard let self, self.isConnected else { return }
            self.audioPlayer.setEnabled(playing && self.audioEnabled)
        }
        pictureInPicture.failed = { [weak self] in
            self?.controlStatus = NSLocalizedString("画中画未能开启，请保持应用在前台后重试。", comment: "")
        }
        announcements.send = { [weak bridge] data, requestID in
            bridge?.sendAnnouncementRequest(data, requestID: requestID)
        }
        if !savesConnectionThumbnails { clearConnectionThumbnails() }
        if hasNetworkConsent { configureBridge() }
    }

    deinit {
        networkPath.cancel()
        privacyTimer?.invalidate()
        presenceTimer?.invalidate()
        remoteVersionCheckTask?.cancel()
    }

    var privacyNoticeVisible: Bool { privacyNotice.isVisible }

    func applicationDidBecomeActive() {
        privacyClock = ProcessInfo.processInfo.systemUptime
        privacyScreen.tick(at: privacyClock)
        appIsActive = true
        if hasNetworkConsent { bridge.verifySignaling() }
        AppOrientation.resume()
        pictureInPicture.didBecomeActive()
        appUpdates.setEnabled(hasNetworkConsent)
        privacyNotice.didBecomeActive(hasNetworkConsent: hasNetworkConsent)
        refreshRecentConnectionPresenceAfterForeground()
        refreshAnnouncements()
        resumeVideoAfterForeground()
    }

    func applicationDidEnterBackground() {
        appIsActive = false
        appUpdates.setEnabled(false)
        privacyNotice.didEnterBackground()
        suspendVideoForBackground()
        suspendPresenceMonitoring()
    }

    func showPrivacyNotice() {
        privacyNotice.show()
    }

    func dismissPrivacyNotice() {
        privacyNotice.dismiss()
    }

    func acceptNetworkConsent() {
        privacyPreferences.setNetworkConsent(true)
        hasNetworkConsent = true
        appUpdates.setEnabled(appIsActive)
        privacyNotice.dismiss()
        configureBridge()
    }

    func declineNetworkConsent() {
        revokeNetworkConsent()
        privacyNotice.dismiss()
    }

    func revokeNetworkConsent() {
        privacyPreferences.setNetworkConsent(false)
        hasNetworkConsent = false
        appUpdates.setEnabled(false)
        signalConnected = false
        stopPresenceMonitoring()
        invalidatePresence()
        resetConnection()
        // disconnect() only closes the remote session. Consent withdrawal must
        // also destroy the persistent signaling peer and its reconnect loop.
        bridge.stopNetworking()
        announcements.reset()
        bridgeConfiguration = nil
        localIdentity = ""
        signalStatus = NSLocalizedString("等待隐私授权", comment: ""); signalHasError = false
        savesConnectionThumbnails = false
        clearConnectionThumbnails()
    }

    func setSavesConnectionThumbnails(_ allowed: Bool) {
        privacyPreferences.setSavesThumbnails(allowed)
        savesConnectionThumbnails = privacyPreferences.savesThumbnails
        if !savesConnectionThumbnails { clearConnectionThumbnails() }
        // Enabling applies on the next connection, never to a frame captured
        // before the user opted in.
    }

    func clearConnectionThumbnails() {
        thumbnailGeneration = UUID()
        shouldCaptureThumbnail = false
        pendingCaptures.removeAll()
        ConnectionHistory.clearThumbnailReferences()
        recentConnections = history.load()
        thumbnailCleanupInProgress = true
        thumbnailCleanupError = nil
        let generation = thumbnailGeneration
        RecentConnectionStore.removeAllThumbnails { [weak self] success in
            guard let self, self.thumbnailGeneration == generation else { return }
            self.thumbnailCleanupInProgress = false
            self.thumbnailCleanupError = success
                ? nil
                : NSLocalizedString("部分预览图未能清除，请重试。", comment: "")
        }
    }

    func setCustomServer(_ server: SignalServerConfiguration?) {
        guard server != customServer else { return }
        SignalServerConfiguration.saveCustom(server)
        customServer = server
        let selected = (server ?? .official).history
        if selected != history {
            if !canStartConnection { resetConnection() }
            if bridgeConfiguration != nil {
                bridge.stopNetworking()
                bridgeConfiguration = nil
            }
            activeHistory = nil
            activeRemoteID = ""
            shouldCaptureThumbnail = false
            pendingRememberPassword = false
            password = ""
            remoteID = ""
            history = selected
            recentConnections = selected.load()
            invalidatePresence()
        }
        configureBridge()
    }

    @discardableResult
    func configureBridge() -> Bool {
        guard hasNetworkConsent else {
            signalStatus = NSLocalizedString("等待隐私授权", comment: ""); signalHasError = false
            return false
        }
        let selected = customServer ?? .official
        // Keep the live peer for equivalent spellings of the same endpoint.
        let configuration = bridgeConfiguration.flatMap {
            $0.historyEndpoint == selected.historyEndpoint ? $0 : nil
        } ?? selected
        if bridgeConfiguration != configuration {
            bridgeConfiguration = configuration
            signalConnected = false
            localIdentity = ""
            announcements.reset()
            signalStatus = NSLocalizedString("正在连接信令服务", comment: ""); signalHasError = false
            stopPresenceMonitoring()
            invalidatePresence()
            cancelPendingPresenceConnection()
        } else if signalConnected {
            signalStatus = NSLocalizedString("已连接服务器", comment: ""); signalHasError = false
        }
        bridge.setVideoAdaptationPolicy(videoAdaptationPolicy.bridgeValue)
        bridge.configure(withSignalHost: configuration.host,
                         signalPort: configuration.signalPort)
        return true
    }

    func connectionTarget(_ input: String) -> String? {
        if input.range(of: "^[0-9]{9}$", options: .regularExpression) != nil { return input }
        if let endpoint = CrossDeskRTCBridge.directEndpoint(input) { return endpoint }
        connectionFailureMessage = NSLocalizedString("请输入设备 ID、IP 或域名", comment: "")
        return nil
    }

    func connect(password: String, rememberPassword: Bool, saved: Bool = false, replacement: String? = nil) {
        guard canStartConnection else { return }
        guard let target = connectionTarget(remoteID.trimmingCharacters(in: .whitespacesAndNewlines)) else { return }
        expectedHostKey = replacement ?? recentConnections.first(where: { $0.id == target })?.boundFingerprint ?? ""
        usingSavedCredential = saved
        verifiedHostKey = ""
        hostKeyChange = nil
        rejectedPasswordPrompt = nil
        self.password = password
        pendingRememberPassword = rememberPassword
        connect()
    }

    private func connect() {
        guard hasNetworkConsent else {
            showPrivacyNotice()
            return
        }
        connectionFailureMessage = nil
        guard let identifier = connectionTarget(remoteID.trimmingCharacters(in: .whitespacesAndNewlines)) else { return }
        remoteID = identifier
        guard configureBridge() else {
            connectionStatus = signalStatus
            return
        }
        if !password.isEmpty || identifier.contains(":") || isDeviceOnline(identifier) {
            beginRemoteConnection(identifier)
        } else {
            beginPresenceProbe(identifier)
        }
    }

    private func beginRemoteConnection(_ identifier: String) {
        guard let configuration = bridgeConfiguration else { return }
        AppOrientation.endSession()
        networkStatistics = RemoteNetworkStatistics()
        resetRemoteHostInfo()
        cancelVideoRecovery()
        resetVideoSettings()
        cancelPresenceProbe()
        activeHistory = history
        activeRemoteID = identifier
        resolutionPreferenceKey = RemoteDisplayResolutionStore.key(host: configuration.host,
            port: configuration.signalPort, remoteID: identifier)
        activeDisplayName = ""
        displaySizes = []
        connectionRecorded = false
        shouldCaptureThumbnail = savesConnectionThumbnails
        isConnecting = true
        isConnected = false
        sessionVisible = false
        resetRemoteCursorState()
        connectionStatus = NSLocalizedString("正在准备连接…", comment: "")
        bridge.connect(toRemoteID: identifier, password: password, expectedKey: expectedHostKey, saved: usingSavedCredential,
                       displayResolutions: RemoteDisplayResolutionStore.message(key: resolutionPreferenceKey))
    }

    private func beginPresenceProbe(_ identifier: String) {
        guard hasNetworkConsent, signalConnected, presenceIsForeground else {
            showDeviceOffline()
            return
        }

        presenceProbeGeneration &+= 1
        let generation = presenceProbeGeneration
        pendingPresenceRemoteID = identifier
        isConnecting = true
        isConnected = false
        sessionVisible = false
        connectionStatus = NSLocalizedString("正在确认设备状态…", comment: "")
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
        connectionStatus = NSLocalizedString("未连接", comment: "")
    }

    private func requestPresenceProbe(_ identifier: String) {
        guard hasNetworkConsent, signalConnected, presenceIsForeground else { return }
        // Older servers ignore subscribe=false and replace the watched list.
        var identifiers = recentDeviceIDs
        if !identifiers.contains(identifier) { identifiers.append(identifier) }
        bridge.requestPresence(remoteIDs: identifiers, subscribe: false)
    }

    private func showDeviceOffline() {
        isConnecting = false
        isConnected = false
        sessionVisible = false
        connectionStatus = NSLocalizedString("设备离线", comment: "")
        deviceOfflineAlertVisible = true
    }

    func dismissDeviceOfflineAlert() {
        deviceOfflineAlertVisible = false
    }

    func dismissConnectionFailure() {
        guard connectionFailureMessage != nil else { return }
        connectionFailureMessage = nil
        hostKeyChange = nil
        rejectedPasswordPrompt = nil
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
        pictureInPicture.endSession()
        clipboardStatus = ""
        controlStatus = ""
        transferStatus = ""
        transferProgress = 0
        receivedFileURL = nil
        resetRemoteHostInfo()
        resetVideoSettings()
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
        networkStatistics = RemoteNetworkStatistics()
        displays = [NSLocalizedString("显示器 1", comment: "")]
        displaySizes = []
        selectedDisplay = 0
        connectionStatus = NSLocalizedString("未连接", comment: "")
        audioPlayer.endSession()
        AppOrientation.endSession()
        appUpdates.checkIfNeeded()
    }

    func retry() {
        resetRemoteHostInfo()
        cancelVideoRecovery()
        bridge.disconnect()
        connect()
    }

    func dismissRemoteUpdate() {
        remoteUpdateMessage = nil
    }

    private func resetRemoteHostInfo() {
        remoteServiceAvailable = false
        remoteVersionCheckTask?.cancel()
        remoteVersionCheckTask = nil
        remoteVersionCheckGeneration = UUID()
        remoteVersionCheckAttempted = false
        remoteAppVersion = ""
        remotePlatform = .unknown
        supportsTextInput = false
        keyboardInputStatus = ""
        remoteHostInfoReceived = false
        remoteUpdateMessage = nil
    }

    private func checkRemoteVersionIfNeeded() {
        guard hasNetworkConsent, isConnected, remoteHostInfoReceived,
              !remoteVersionCheckAttempted else { return }
        remoteVersionCheckAttempted = true
        if remoteAppVersion.isEmpty {
            remoteUpdateMessage = Self.remoteUpdateNotice
            return
        }
        let generation = remoteVersionCheckGeneration
        let appVersion = remoteAppVersion
        // Only public release metadata is fetched; no peer ID or version is sent.
        let url = URL(string: "https://version.crossdesk.cn/version.json")!
        let request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData,
                                 timeoutInterval: 15)
        remoteVersionCheckTask = URLSession.shared.dataTask(with: request) {
            [weak self] data, response, error in
            let latest: String?
            if error == nil, let data,
               (response as? HTTPURLResponse)?.statusCode == 200 {
                latest = CrossDeskRTCBridge.availableUpdate(appVersion: appVersion,
                                                            releaseJSON: data)
            } else {
                latest = nil
            }
            DispatchQueue.main.async { [weak self] in
                guard let self,
                      self.remoteVersionCheckGeneration == generation,
                      self.isConnected else { return }
                self.remoteVersionCheckTask = nil
                guard latest != nil else { return }
                self.remoteUpdateMessage = Self.remoteUpdateNotice
            }
        }
        remoteVersionCheckTask?.resume()
    }

    func savedPassword(for remoteID: String) -> String {
        savedCredential(for: remoteID) ?? ""
    }

    private func credentialAccount(for remoteID: String) -> String {
        let record = recentConnections.first { $0.id == remoteID }
        if let ref = record?.credentialRef {
            return history.account(for: remoteID) + ":domain-v1:" + ref
        }
        if record?.usesLegacyPassword == true {
            return remoteID
        }
        return history.account(for: remoteID)
    }

    func savedCredential(for remoteID: String) -> String? {
        guard remembersPassword(for: remoteID), history.account(for: remoteID) != rejectedCredentialAccount else { return nil }
        let value = ConnectionCredentialStore.password(for: credentialAccount(for: remoteID))
        guard CrossDeskRTCBridge.isDomainEndpoint(remoteID) else { return value }
        guard let record = recentConnections.first(where: { $0.id == remoteID }), record.credentialRef != nil,
              let pin = record.boundFingerprint, let value, value.hasPrefix(pin + ":") else { return nil }
        let password = String(value.dropFirst(65))
        return password.range(of: "^[A-Za-z0-9]{6}$", options: .regularExpression) != nil ? password : nil
    }

    func remembersPassword(for remoteID: String) -> Bool {
        recentConnections.first(where: { $0.remoteID == remoteID })?
            .remembersPassword == true
    }

    func thumbnailImage(for connection: RecentConnection) -> UIImage? {
        guard savesConnectionThumbnails,
              let fileName = connection.thumbnailFileName,
              fileName == history.thumbnailName(for: connection.id) else {
            return nil
        }
        return UIImage(contentsOfFile: RecentConnectionStore.thumbnailURL(fileName: fileName).path)
    }

    func isRecentConnectionOnline(_ connection: RecentConnection) -> Bool {
        isDeviceOnline(connection.remoteID)
    }

    private func isDeviceOnline(_ identifier: String) -> Bool {
        guard hasNetworkConsent, signalConnected, presenceIsForeground, !awaitingPresenceSnapshot,
              recentConnectionPresence[identifier] == true,
              let updatedAt = presenceUpdatedAt[identifier] else { return false }
        return updatedAt.duration(to: presenceClock.now) < Self.presenceMaxAge
    }

    private var recentDeviceIDs: [String] {
        recentConnections.map(\.remoteID).filter { !$0.contains(":") }
    }

    private func refreshRecentConnectionPresence() {
        guard hasNetworkConsent, signalConnected, presenceIsForeground else { return }
        lastPresenceRefreshAt = presenceClock.now
        bridge.requestPresence(remoteIDs: recentDeviceIDs,
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
        guard hasNetworkConsent, signalConnected, presenceIsForeground, presenceTimer == nil else { return }
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
        guard hasNetworkConsent, signalConnected, presenceIsForeground else { return }
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
        networkStatistics.resetVideo()
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

    func editRecentConnection(_ id: String, alias: String? = nil, groups: [String]? = nil) {
        guard let index = recentConnections.firstIndex(where: { $0.id == id }) else { return }
        if let alias {
            recentConnections[index].alias = alias.trimmingCharacters(in: .whitespacesAndNewlines)
        }
        if let groups {
            recentConnections[index].groups = Array(Set(groups)).sorted()
        }
        history.save(recentConnections)
    }

    func deleteGroup(_ name: String, in scope: ConnectionHistory) {
        guard scope == history, !name.isEmpty else { return }
        var updated = recentConnections
        for index in updated.indices { updated[index].groups.removeAll { $0 == name } }
        guard history.save(updated) else {
            connectionFailureMessage = NSLocalizedString("保存失败，请重试", comment: "")
            return
        }
        recentConnections = updated
    }

    func removeRecentConnection(_ connection: RecentConnection) {
        let account = credentialAccount(for: connection.id)
        let updated = recentConnections.filter { $0.remoteID != connection.remoteID }
        if connection.credentialRef != nil {
            guard history.save(updated) else { return }
            recentConnections = updated
            if activeRemoteID == connection.id { connectionRecorded = true }
        }
        guard ConnectionCredentialStore.removePassword(for: account) else {
            connectionFailureMessage = NSLocalizedString("安全存储不可用，请解锁设备后重试", comment: "")
            return
        }
        recentConnections = updated
        if activeRemoteID == connection.id { connectionRecorded = true }
        recentConnectionPresence.removeValue(forKey: connection.remoteID)
        presenceUpdatedAt.removeValue(forKey: connection.remoteID)
        history.save(recentConnections)
        pendingCaptures.removeValue(forKey: history.account(for: connection.id))
        if activeRemoteID == connection.id { shouldCaptureThumbnail = false }
        RecentConnectionStore.removeThumbnail(named: history.thumbnailName(for: connection.id))
        refreshRecentConnectionPresence()
    }

    private func recordSuccessfulConnectionIfNeeded() {
        guard activeHistory == history, !connectionRecorded, !activeRemoteID.isEmpty else { return }
        connectionRecorded = true

        let previous = recentConnections.first { $0.remoteID == activeRemoteID }
        let oldAccount = credentialAccount(for: activeRemoteID)
        let domain = CrossDeskRTCBridge.isDomainEndpoint(activeRemoteID)
        let ref = domain && pendingRememberPassword ? UUID().uuidString : nil
        let account = ref.map { history.account(for: activeRemoteID) + ":domain-v1:" + $0 } ?? oldAccount
        let secret = domain ? verifiedHostKey + ":" + password : password
        let remembered = pendingRememberPassword && (!domain || verifiedHostKey.count == 64) &&
            ConnectionCredentialStore.save(password: secret, for: account)
        if domain && pendingRememberPassword && !remembered { return }
        let title = activeDisplayName.isEmpty
            ? (previous?.hostName ?? activeRemoteID)
            : activeDisplayName
        var connection = RecentConnection(
            remoteID: activeRemoteID,
            displayName: title,
            lastConnectedAt: Date(),
            remembersPassword: remembered,
            thumbnailFileName: previous?.thumbnailFileName,
            platform: remoteHostInfoReceived ? remotePlatform : (previous?.platform ?? .unknown),
            alias: previous?.alias ?? "",
            groups: previous?.groups ?? []
        )
        connection.usesLegacyPassword = previous?.usesLegacyPassword
        connection.credentialRef = ref
        connection.fingerprint = domain && remembered ? verifiedHostKey : nil
        var updated = recentConnections.filter { $0.remoteID != activeRemoteID }
        updated.insert(connection, at: 0)
        guard history.save(updated) else {
            if ref != nil { ConnectionCredentialStore.removePassword(for: account) }
            return
        }
        recentConnections = updated
        if history.account(for: activeRemoteID) == rejectedCredentialAccount { rejectedCredentialAccount = nil }
        if !pendingRememberPassword || (domain && previous?.credentialRef != nil && account != oldAccount) {
            ConnectionCredentialStore.removePassword(for: oldAccount)
        }
        refreshRecentConnectionPresence()
    }

    private func updateActiveConnectionHostInfo(_ name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard activeHistory == history, !activeRemoteID.isEmpty else { return }
        if !trimmed.isEmpty { activeDisplayName = trimmed }
        guard let index = recentConnections.firstIndex(where: {
            $0.remoteID == activeRemoteID
        }) else { return }
        recentConnections[index].updateHostInfo(name: trimmed, platform: remotePlatform)
        history.save(recentConnections)
    }

    private func captureRecentThumbnailIfNeeded(_ pixelBuffer: CVPixelBuffer) {
        guard hasNetworkConsent, savesConnectionThumbnails,
              let scope = activeHistory, scope == history, shouldCaptureThumbnail, !activeRemoteID.isEmpty else { return }
        shouldCaptureThumbnail = false
        let identifier = activeRemoteID
        let account = scope.account(for: identifier)
        let token = UUID()
        pendingCaptures[account] = token
        RecentConnectionStore.captureThumbnail(from: pixelBuffer, name: scope.thumbnailName(for: identifier)) { [weak self] name in
            guard let self, self.pendingCaptures[account] == token else { return }
            self.pendingCaptures.removeValue(forKey: account)
            guard let name, self.savesConnectionThumbnails else { return }
            var records = scope.load()
            guard let index = records.firstIndex(where: { $0.id == identifier }) else { return }
            records[index].thumbnailFileName = name
            scope.save(records)
            if self.history == scope { self.recentConnections = records }
        }
    }

    func selectDisplay(_ index: Int) {
        guard displays.indices.contains(index) else { return }
        selectedDisplay = index
        networkStatistics.resetVideo()
        videoFrame = nil
        resetRemoteCursorState()
        frameCount = 0
        bridge.switch(toDisplay: index)
    }

    var canChangeVideoSettings: Bool {
        isConnected && videoSettings.supported
    }

    var videoSettingsFeedback: String {
        if !canChangeVideoSettings {
            return NSLocalizedString("连接建立后可调整，需被控端支持。", comment: "")
        }
        if videoSettings.failed { return NSLocalizedString("调整失败，请重试。", comment: "") }
        return ""
    }

    func updateVideoSettings(quality: Int? = nil, frameRate: Int? = nil,
                             preference: Int? = nil) {
        guard hasNetworkConsent, canChangeVideoSettings else { return }
        var settings = videoSettings.selection
        if let quality { settings.quality = quality }
        if let frameRate { settings.frameRate = frameRate }
        if let preference { settings.preference = preference }
        guard let requestID = videoSettings.begin(settings) else { return }
        videoSettingsTimeoutTask?.cancel()
        bridge.sendVideoSettings(quality: settings.quality,
                                 frameRate: settings.frameRate,
                                 preference: settings.preference,
                                 requestID: requestID)
        videoSettingsTimeoutTask = Task { @MainActor [weak self] in
            do {
                try await Task.sleep(nanoseconds: 5_000_000_000)
            } catch { return }
            self?.videoSettings.expire(requestID: requestID)
        }
    }

    func updateDisplayResolution(_ resolution: RemoteDisplayResolution, displayID: Int, displayKey: String) {
        guard hasNetworkConsent, isConnected, selectedDisplay == displayID,
              let display = displayResolutions.display(displayID), display.display_key == displayKey else { return }
        if !displayResolutions.pending, display.current == resolution, display.modes.contains(resolution) {
            RemoteDisplayResolutionStore.remember(display.preference, key: resolutionPreferenceKey)
            return
        }
        guard let request = displayResolutions.begin(displayID: selectedDisplay, resolution: resolution) else { return }
        displayResolutionTimeoutTask?.cancel()
        bridge.sendDisplayResolution(displayID: request.display_id, displayKey: request.display_key,
                                     width: request.width, height: request.height, requestID: request.request_id)
        displayResolutionTimeoutTask = Task { @MainActor [weak self] in
            do { try await Task.sleep(nanoseconds: 10_000_000_000) } catch { return }
            self?.displayResolutions.expire(requestID: request.request_id)
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveDisplayResolutions message: Data) {
        // Host information can arrive before the connection-state callback.
        // The bridge already rejects callbacks from previous sessions.
        guard hasNetworkConsent else { return }
        if let confirmed = displayResolutions.receive(message) {
            RemoteDisplayResolutionStore.remember(confirmed, key: resolutionPreferenceKey)
        }
        if !displayResolutions.pending {
            displayResolutionTimeoutTask?.cancel(); displayResolutionTimeoutTask = nil
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didFailDisplayResolutionRequest requestID: UInt32) {
        guard hasNetworkConsent, isConnected else { return }
        displayResolutions.expire(requestID: requestID)
        if !displayResolutions.pending {
            displayResolutionTimeoutTask?.cancel(); displayResolutionTimeoutTask = nil
        }
    }

    private func resetVideoSettings() {
        displayResolutionTimeoutTask?.cancel(); displayResolutionTimeoutTask = nil
        displayResolutions = RemoteDisplayResolutionState()
        privacyTimer?.invalidate(); privacyTimer = nil
        privacyScreen = RemotePrivacyScreen()
        privacyClock = ProcessInfo.processInfo.systemUptime
        videoSettingsTimeoutTask?.cancel()
        videoSettingsTimeoutTask = nil
        videoSettings = RemoteVideoSettingsState(
            preference: videoAdaptationPolicy.bridgeValue.rawValue)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didReceiveVideoSettingsQuality quality: Int,
                   frameRate: Int, preference: Int,
                   requestID: UInt32, accepted: Bool) {
        guard hasNetworkConsent, isConnected else { return }
        videoSettings.receive(RemoteVideoSettings(quality: quality,
                                                  frameRate: frameRate,
                                                  preference: preference),
                              requestID: requestID, accepted: accepted)
        if !videoSettings.pending {
            videoSettingsTimeoutTask?.cancel()
            videoSettingsTimeoutTask = nil
        }
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
        guard hasNetworkConsent, isConnected else { return }
        guard let text = UIPasteboard.general.string, !text.isEmpty else {
            clipboardStatus = NSLocalizedString("剪贴板中没有文本", comment: "")
            return
        }
        guard text.lengthOfBytes(using: .utf8) <= 128 * 1024 else {
            clipboardStatus = NSLocalizedString("剪贴板文本超过 128 KiB", comment: "")
            return
        }
        bridge.sendClipboardText(text)
        clipboardStatus = NSLocalizedString("已发送本机剪贴板", comment: "")
    }

    func sendFile(_ url: URL) {
        guard hasNetworkConsent, isConnected else { return }
        transferStatus = String(format: NSLocalizedString("正在发送 %1$@", comment: ""), String(describing: url.lastPathComponent))
        transferProgress = 0
        bridge.sendFile(at: url)
    }

    private func startPrivacyMonitoring() {
        guard privacyTimer == nil else { return }
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.privacyClock = ProcessInfo.processInfo.systemUptime
            self.privacyScreen.tick(at: self.privacyClock)
        }
        RunLoop.main.add(timer, forMode: .common)
        privacyTimer = timer
    }

    func lockPrivacyScreen() { sendPrivacyScreen(lock: true) }
    func togglePrivacyScreen() { sendPrivacyScreen(lock: false) }
    private func sendPrivacyScreen(lock: Bool) {
        guard hasNetworkConsent, isConnected else { return }
        privacyClock = ProcessInfo.processInfo.systemUptime
        let enable = !privacyScreen.active
        guard let request = privacyScreen.begin(at: privacyClock, lock: lock) else { return }
        bridge.sendPrivacyCommand(enable, lock: lock, blockLocalInput: true, requestID: request)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceivePrivacyStatus data: Data) {
        guard isConnected || isConnecting else { return }
        privacyClock = ProcessInfo.processInfo.systemUptime
        privacyScreen.receive(data, at: privacyClock)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didSendPrivacyRequest requestID: UInt32, success: Bool) {
        if !success { privacyScreen.failed(request: requestID) }
    }

    func sendSecureAttention() {
        guard hasNetworkConsent, isConnected, remotePlatform == .windows else { return }
        guard remoteServiceAvailable else {
            controlStatus = NSLocalizedString("Ctrl+Alt+Del 需要远端启用 Windows 系统服务", comment: "")
            return
        }
        bridge.sendSecureAttentionSequence()
        controlStatus = NSLocalizedString("正在发送 Ctrl+Alt+Del…", comment: "")
    }

    func fileImportFailed() {
        transferStatus = NSLocalizedString("无法读取所选文件，请重试", comment: "")
        transferProgress = 0
    }

    /// Validate the full edit before deleting anything at the remote caret.
    /// Corrections and dictation revisions share the ordered keyboard stream.
    func sendSystemTextEdit(deleting count: Int, inserting text: String) -> Bool {
        guard hasNetworkConsent, isConnected else { return false }
        guard (0...16 * 1024).contains(count), text.utf8.count <= 16 * 1024, !text.unicodeScalars.contains(where: { $0.value == 0 }) else {
            keyboardInputStatus = NSLocalizedString("单次输入最多 16 KiB，且不能包含空字符", comment: "")
            return false
        }
        let isASCII = text.unicodeScalars.allSatisfy({
            (0x20...0x7e).contains($0.value) || [9, 10, 13].contains($0.value)
        })
        guard supportsTextInput || isASCII else {
            keyboardInputStatus = NSLocalizedString("被控端不支持直接输入中文，请更新 macOS 被控端；也可先复制文字，通过远控菜单的剪贴板发送后粘贴", comment: "")
            return false
        }
        for _ in 0..<count { sendKeyStroke(0x08) }
        if !text.isEmpty {
            if supportsTextInput {
                bridge.sendCommittedText(text)
            } else {
                bridge.sendText(text.replacingOccurrences(of: "\r\n", with: "\n")
                    .replacingOccurrences(of: "\r", with: "\n"))
            }
        }
        keyboardInputStatus = ""
        return true
    }

    func sendKeyStroke(_ keyCode: UInt) {
        sendKeyChord(keyCode, modifiers: [])
    }

    func sendKeyChord(_ keyCode: UInt, modifiers: [UInt]) {
        guard hasNetworkConsent, isConnected, !privacyScreen.unlockNotice else { return }
        bridge.sendKeyChord(keyCode, modifiers: RemoteShortcut.modifierKeys.filter(modifiers.contains).map { NSNumber(value: $0) })
    }

    func showKeyboardCompositionHint() {
        keyboardInputStatus = NSLocalizedString("请先选词或取消组词", comment: "")
    }

    func clearKeyboardInputStatus() { keyboardInputStatus = "" }

    func sendShortcut(_ item: RemoteShortcut) {
        guard hasNetworkConsent, isConnected, !privacyScreen.unlockNotice,
              item.platform == remotePlatform.rawValue else { return }
        keyboardInputStatus = ""
        if item.service {
            sendSecureAttention()
            keyboardInputStatus = controlStatus
        } else {
            sendKeyChord(item.key, modifiers: item.modifiers)
        }
    }

    func sendKeyState(_ keyCode: UInt, isDown: Bool) {
        guard hasNetworkConsent, isConnected else { return }
        bridge.sendWindowsKeyCode(keyCode, isDown: isDown)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didChangeIdentityStorageError hasError: Bool) {
        identityStorageWarning = hasError
            ? NSLocalizedString("本机登录凭据暂时无法安全存取。请解锁设备后，在设置中点击“应用”重试。", comment: "")
            : nil
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didChange state: CrossDeskSignalState) {
        guard hasNetworkConsent else { return }
        signalConnected = state.rawValue == 1
        announcements.setConnected(signalConnected)
        invalidatePresence()
        switch state.rawValue {
        case 0: signalStatus = NSLocalizedString("正在连接信令服务", comment: ""); signalHasError = false
        case 1: signalStatus = NSLocalizedString("已连接服务器", comment: ""); signalHasError = false
        case 2: signalStatus = NSLocalizedString("信令连接失败", comment: ""); signalHasError = true
        case 3: signalStatus = NSLocalizedString("信令连接已关闭", comment: ""); signalHasError = true
        case 4: signalStatus = NSLocalizedString("信令服务重连中", comment: ""); signalHasError = false
        case 5: signalStatus = NSLocalizedString("信令服务器已关闭连接", comment: ""); signalHasError = true
        case 6: signalStatus = NSLocalizedString("TLS 证书校验失败", comment: ""); signalHasError = true
        case 7: signalStatus = NSLocalizedString("无法访问本机登录凭据，请解锁后重试", comment: ""); signalHasError = true
        default: signalStatus = NSLocalizedString("未知信令状态", comment: ""); signalHasError = true
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

    func rtcBridge(_ bridge: CrossDeskRTCBridge, joinDelivery status: String, remoteID: String) {
        guard isConnecting, remoteID == activeRemoteID else { return }
        if status == "unresponsive" {
            finishConnection(withError: NSLocalizedString("对端未响应，请确认设备在线后重试。", comment: ""))
        } else {
            connectionStatus = status == "checking" ? NSLocalizedString("正在确认设备状态…", comment: "")
                : NSLocalizedString("正在建立远程连接", comment: "")
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didChange state: CrossDeskConnectionState,
                   remoteID: String, detail: String) {
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
        case 0: connectionStatus = NSLocalizedString("正在建立远程连接", comment: "")
        case 1:
            verifiedHostKey = detail
            connectionStatus = NSLocalizedString("已连接", comment: "")
            startPrivacyMonitoring()
            sessionVisible = true
            pictureInPicture.beginSession()
            if videoSettings.requestID == 0 { updateVideoSettings() }
            checkRemoteVersionIfNeeded()
            recordSuccessfulConnectionIfNeeded()
            audioPlayer.setEnabled(audioEnabled)
            bridge.setAudioEnabled(audioEnabled)
            AppOrientation.beginSession(autoLandscape: autoLandscape)
        case 2: connectionStatus = NSLocalizedString("正在收集 ICE 候选", comment: "")
        case 3: finishConnection(withError: NSLocalizedString("网络连接已中断，请重新连接。", comment: ""))
        case 4:
            if detail.hasPrefix("HostKeyChanged:") {
                let key = String(detail.dropFirst(15))
                let change = HostKeyChange(target: activeRemoteID, expected: expectedHostKey, observed: key)
                finishConnection(withError: NSLocalizedString("设备身份已变化。核对以下公钥指纹后，重新输入密码。", comment: "") + "\n" + expectedHostKey + "\n↓\n" + key)
                hostKeyChange = change
                return
            }
            if !detail.isEmpty {
                let lines = detail.components(separatedBy: "\n").map { NSLocalizedString($0, comment: "") }
                finishConnection(withError: lines.joined(separator: "\n"))
                return
            }
            let message = activeRemoteID.contains(":") ? NSLocalizedString("IP 直连失败，请检查电脑的直连地址和网络。", comment: "") :
                NSLocalizedString("P2P/TURN 建链失败，请检查网络后重试。", comment: "")
            finishConnection(withError: message)
        case 5:
            finishConnection(withError: NSLocalizedString("远程连接已结束。", comment: ""))
        case 6:
            let target = activeRemoteID
            let retry = CrossDeskRTCBridge.isDomainEndpoint(target)
                ? DirectPasswordPrompt(target: target, expectedKey: expectedHostKey) : nil
            if retry != nil && usingSavedCredential { rejectedCredentialAccount = history.account(for: target) }
            finishConnection(withError: NSLocalizedString("访问密码错误，请重新输入。", comment: ""))
            rejectedPasswordPrompt = retry
        case 7:
            if !remoteID.isEmpty {
                recentConnectionPresence[remoteID] = false
                presenceUpdatedAt.removeValue(forKey: remoteID)
            }
            finishConnection(withError: NSLocalizedString("远程设备 ID 不存在，请检查设备 ID。", comment: ""))
        case 8:
            if !remoteID.isEmpty {
                recentConnectionPresence[remoteID] = false
                presenceUpdatedAt.removeValue(forKey: remoteID)
            }
            finishConnection(withError: NSLocalizedString("远程设备当前不可用，请稍后重试。", comment: ""))
        default: connectionStatus = NSLocalizedString("未知连接状态", comment: "")
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didProvisionIdentity identity: String) {
        guard hasNetworkConsent else { return }
        localIdentity = identity.split(separator: "@").first.map(String.init) ?? identity
        if let configuration = bridgeConfiguration {
            announcements.configure(host: configuration.host, port: configuration.signalPort,
                                    deviceID: localIdentity)
        }
        refreshRecentConnectionPresence()
        if let pendingPresenceRemoteID {
            requestPresenceProbe(pendingPresenceRemoteID)
        }
    }

    var announcementsConnected: Bool { hasNetworkConsent && signalConnected }

    func refreshAnnouncements() {
        guard announcementsConnected else { return }
        announcements.refresh()
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveAnnouncementMessage message: Data) {
        guard announcementsConnected else { return }
        let object = (try? JSONSerialization.jsonObject(with: message)) as? [String: Any]
        if object?["type"] as? String == "announcements_changed" {
            announcements.catalogChanged(resetPages: object?["reset_pages"] as? Bool == true)
        } else {
            announcements.receive(message)
        }
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didFailAnnouncementRequest requestID: String) {
        guard hasNetworkConsent else { return }
        announcements.failed(requestID: requestID)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didReceivePresence presence: [String: NSNumber],
                   snapshot: Bool) {
        guard hasNetworkConsent, signalConnected, presenceIsForeground,
              snapshot || !awaitingPresenceSnapshot else { return }
        var updated = snapshot ? [:] : recentConnectionPresence
        if snapshot {
            presenceUpdatedAt = [:]
            awaitingPresenceSnapshot = false
        }
        let now = presenceClock.now
        let watched = Set(recentDeviceIDs)
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
                   height: Int,
                   captureUptime: TimeInterval) {
        guard hasNetworkConsent, isConnected else { return }
        if videoWasBackgrounded {
            // Feed PiP directly, without publishing background SwiftUI updates.
            if pictureInPicture.display(pixelBuffer) {
                videoFrameID &+= 1
                networkStatistics.recordSubmittedFrame(id: videoFrameID, captureUptime: captureUptime,
                                                       at: ProcessInfo.processInfo.systemUptime)
            }
            return
        }
        cancelVideoRecovery()
        // Buffer and encoded dimensions must be one observable value. Adaptive
        // resolution changes must never expose a new frame with the previous
        // frame's geometry to SwiftUI.
        videoFrameID &+= 1
        videoFrame = RemoteVideoFrame(
            pixelBuffer: pixelBuffer,
            encodedSize: CGSize(width: width, height: height),
            id: videoFrameID,
            captureUptime: captureUptime
        )
        frameCount &+= 1
        captureRecentThumbnailIfNeeded(pixelBuffer)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didReceiveHostName hostName: String,
                   appVersion: String,
                   platform: String,
                   displayNames remoteDisplayNames: [String],
                   displaySizes remoteDisplaySizes: [NSValue],
                   supportsVideoSettings: Bool,
                   supportsTextInput: Bool) {
        self.supportsTextInput = supportsTextInput
        let previouslySupported = videoSettings.supported
        videoSettings.setSupported(supportsVideoSettings)
        if !supportsVideoSettings {
            videoSettingsTimeoutTask?.cancel()
            videoSettingsTimeoutTask = nil
        } else if !previouslySupported {
            updateVideoSettings()
        }
        remoteHostInfoReceived = true
        remoteAppVersion = appVersion
        remotePlatform = RemoteHostPlatform(rawValue: platform) ?? .unknown
        checkRemoteVersionIfNeeded()
        updateActiveConnectionHostInfo(hostName)
        let names = remoteDisplayNames.enumerated().map { index, displayName in
            let name = displayName.trimmingCharacters(in: .whitespacesAndNewlines)
            return name.isEmpty ? String(format: NSLocalizedString("显示器 %1$@", comment: ""), String(describing: index + 1)) : name
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
        guard hasNetworkConsent, isConnected else { return }
        audioPlayer.enqueue(pcmData)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveClipboardText text: String) {
        guard hasNetworkConsent, isConnected else { return }
        UIPasteboard.general.string = text
        clipboardStatus = NSLocalizedString("已接收远端剪贴板", comment: "")
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveServiceAvailable available: Bool,
                   interactiveStage stage: String) {
        guard hasNetworkConsent, isConnecting || isConnected else { return }
        remoteServiceAvailable = available
        privacyScreen.setInteractiveStage(stage)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didSendSecureAttention success: Bool) {
        guard hasNetworkConsent, isConnected else { return }
        controlStatus = success ? NSLocalizedString("已发送 Ctrl+Alt+Del", comment: "") : NSLocalizedString("指令发送失败，请重试", comment: "")
        keyboardInputStatus = controlStatus
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didUpdateFileTransfer fileName: String,
                   progress: Double,
                   sending: Bool) {
        guard hasNetworkConsent, isConnected else { return }
        if progress < 0 {
            transferStatus = String(format: NSLocalizedString("%1$@ 传输失败", comment: ""), String(describing: fileName))
            transferProgress = 0
            return
        }
        transferProgress = progress
        let percent = Int((progress * 100).rounded())
        transferStatus = "\(NSLocalizedString(sending ? "发送" : "接收", comment: "")) \(fileName) · \(percent)%"
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge, didReceiveFileAt fileURL: URL) {
        guard hasNetworkConsent, isConnected else { return }
        receivedFileURL = fileURL
        transferStatus = String(format: NSLocalizedString("已接收 %1$@", comment: ""), String(describing: fileURL.lastPathComponent))
        transferProgress = 1
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didUpdateRemoteVideoEncoder encoder: String) {
        guard hasNetworkConsent, isConnected else { return }
        networkStatistics.receiveRemoteVideoEncoder(encoder)
    }

    func rtcBridge(_ bridge: CrossDeskRTCBridge,
                   didUpdateNetworkStats stats: CrossDeskNetworkStats,
                   localDecoder: String, localEndpoint: String, remoteEndpoint: String) {
        guard hasNetworkConsent, isConnected else { return }
        func traffic(_ value: CrossDeskTrafficStats) -> RemoteTrafficStatistics {
            RemoteTrafficStatistics(inboundBitrate: value.inboundBitrate,
                                    outboundBitrate: value.outboundBitrate,
                                    lossRate: value.lossRate)
        }
        let mode: RemoteNetworkReport.Mode
        switch stats.traversalMode {
        case .direct: mode = .direct
        case .relay: mode = .relay
        default: mode = .unknown
        }
        networkStatistics.receive(RemoteNetworkReport(
            video: traffic(stats.video), audio: traffic(stats.audio),
            data: traffic(stats.data), total: traffic(stats.total), mode: mode,
            srtpActive: stats.srtpActive.boolValue, rttMilliseconds: stats.rttMilliseconds,
            localVideoDecoder: localDecoder, localEndpoint: localEndpoint, remoteEndpoint: remoteEndpoint
        ), at: ProcessInfo.processInfo.systemUptime)
    }

    // Called after the exact frame enters AVSampleBufferDisplayLayer. Keep this
    // accumulator unpublished: submitting pixels occurs during a SwiftUI update.
    func recordVideoFrameSubmission(id: UInt64, captureUptime: TimeInterval) {
        guard isConnected, !videoWasBackgrounded, id == videoFrame?.id else { return }
        networkStatistics.recordSubmittedFrame(id: id, captureUptime: captureUptime,
                                               at: ProcessInfo.processInfo.systemUptime)
    }

    var networkSnapshot: RemoteNetworkSnapshot {
        guard isConnected else { return RemoteNetworkSnapshot() }
        return networkStatistics.displaySnapshot(at: ProcessInfo.processInfo.systemUptime)
    }

    var videoStatus: String {
        if frameCount > 0 {
            return String(format: NSLocalizedString("视频 %1$@×%2$@ · %3$@ 帧", comment: ""), String(describing: Int(frameSize.width)), String(describing: Int(frameSize.height)), String(describing: frameCount))
        }
        return isConnected ? NSLocalizedString("正在取回远程画面…", comment: "") : NSLocalizedString("等待连接", comment: "")
    }
}
