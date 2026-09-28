import Foundation

struct RemoteTrafficStatistics {
    var inboundBitrate: UInt32 = 0
    var outboundBitrate: UInt32 = 0
    var lossRate: Float = 0
}

struct RemoteNetworkReport {
    enum Mode { case unknown, direct, relay }

    var video = RemoteTrafficStatistics()
    var audio = RemoteTrafficStatistics()
    var data = RemoteTrafficStatistics()
    var total = RemoteTrafficStatistics()
    var mode: Mode = .unknown
    var srtpActive = false
    var rttMilliseconds: Double = -1
}

struct RemoteNetworkSnapshot {
    var report: RemoteNetworkReport?
    var framesPerSecond = 0
    var videoLatencyMilliseconds: Double?
    var rttMilliseconds: Double?

    var connectionMode: String {
        switch report?.mode {
        case .direct: return "P2P 直连"
        case .relay: return "TURN 中继"
        default: return "—"
        }
    }

    static func bitrate(_ value: UInt32?) -> String {
        guard let value else { return "—" }
        if value >= 1_000_000 {
            return String(format: "%.1f Mbps", Double(value) / 1_000_000)
        }
        if value >= 1_000 {
            return String(format: "%.0f Kbps", Double(value) / 1_000)
        }
        return "\(value) bps"
    }

    static func lossRate(_ value: Float?) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—" }
        // MiniRTC's total is the sum of media fractions, as on desktop.
        return String(format: "%.1f%%", value * 100)
    }

    static func latency(_ value: Double?) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—" }
        return value < 1 ? "<1 ms" : String(format: "%.0f ms", value)
    }
}

/// Main-thread presentation statistics. Time is monotonic and injectable so
/// missing reports, idle video and reconnections have deterministic behavior.
struct RemoteNetworkStatistics {
    private var report: RemoteNetworkReport?
    private var reportTime: TimeInterval = 0
    private var averageRTT: Double?
    private var rttTime: TimeInterval = 0
    private var lastFrameID: UInt64 = 0
    private var frames: [(time: TimeInterval, latency: Double?)] = []
    private var displayedSnapshot = RemoteNetworkSnapshot()
    private var displayTime: TimeInterval?

    mutating func receive(_ report: RemoteNetworkReport, at now: TimeInterval) {
        self.report = report
        reportTime = now
        let rtt = report.rttMilliseconds
        guard rtt.isFinite, rtt >= 0, rtt <= 2_000 else { return }
        if let previous = averageRTT, now - rttTime < 3 {
            averageRTT = previous + 0.25 * (rtt - previous)
        } else {
            averageRTT = rtt
        }
        rttTime = now
    }

    mutating func recordSubmittedFrame(id: UInt64, captureUptime: TimeInterval,
                                       at now: TimeInterval) {
        // Cached redraws and foreground resubmission are not new video frames.
        guard id > lastFrameID else { return }
        lastFrameID = id
        let latency = (now - captureUptime) * 1_000
        let valid = captureUptime > 0 && latency.isFinite && (0...5_000).contains(latency)
        frames.removeAll { now - $0.time >= 1 }
        frames.append((now, valid ? latency : nil))
        if frames.count > 240 { frames.removeFirst(frames.count - 240) }
    }

    mutating func resetVideo() {
        frames.removeAll(keepingCapacity: true)
        displayedSnapshot.framesPerSecond = 0
        displayedSnapshot.videoLatencyMilliseconds = nil
    }

    /// SwiftUI also redraws this menu for every video frame. A periodic view
    /// schedules updates but does not throttle those redraws, so freeze the
    /// displayed measurements between one-second samples.
    mutating func displaySnapshot(at now: TimeInterval) -> RemoteNetworkSnapshot {
        if let displayTime, now - displayTime < 1 {
            return displayedSnapshot
        }
        displayedSnapshot = snapshot(at: now)
        displayTime = now
        return displayedSnapshot
    }

    func snapshot(at now: TimeInterval) -> RemoteNetworkSnapshot {
        let recentFrames = frames.filter { now - $0.time < 1 }
        let delays = recentFrames.compactMap(\.latency)
        return RemoteNetworkSnapshot(
            report: now - reportTime < 3 ? report : nil,
            framesPerSecond: recentFrames.count,
            videoLatencyMilliseconds: delays.isEmpty ? nil : delays.reduce(0, +) / Double(delays.count),
            rttMilliseconds: now - rttTime < 3 ? averageRTT : nil
        )
    }
}
