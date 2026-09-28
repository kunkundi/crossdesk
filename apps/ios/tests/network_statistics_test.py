"""Exercise production statistics with deterministic monotonic time."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "requires the Apple Swift runtime")
class NetworkStatisticsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="crossdesk-network-statistics-")
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        harness = root / "NetworkStatisticsTests.swift"
        harness.write_text(r'''
import Foundation

func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}

func near(_ value: Double?, _ expected: Double) -> Bool {
    guard let value else { return false }
    return abs(value - expected) < 0.0001
}

@main
enum NetworkStatisticsTests {
    static func main() {
        var state = RemoteNetworkStatistics()
        var report = RemoteNetworkReport(
            video: RemoteTrafficStatistics(inboundBitrate: 8_000_000, outboundBitrate: 0, lossRate: 0.02),
            audio: RemoteTrafficStatistics(inboundBitrate: 64_000, outboundBitrate: 0, lossRate: 0.01),
            data: RemoteTrafficStatistics(inboundBitrate: 2_000, outboundBitrate: 3_000, lossRate: 0.03),
            total: RemoteTrafficStatistics(inboundBitrate: 8_066_000, outboundBitrate: 3_000, lossRate: 0.06),
            mode: .relay, srtpActive: true, rttMilliseconds: 20
        )
        let test = CommandLine.arguments[1]
        if test == "report" {
            expect(state.snapshot(at: 10).report == nil, "No measurements before a report")
            expect(state.snapshot(at: 10).connectionMode == "—", "Unknown is not P2P")
            state.receive(report, at: 10)
            let snapshot = state.snapshot(at: 10)
            expect(snapshot.report?.video.inboundBitrate == 8_000_000, "Video rate survives")
            expect(snapshot.report?.audio.inboundBitrate == 64_000, "Audio rate survives")
            expect(snapshot.report?.data.outboundBitrate == 3_000, "Data upload survives")
            expect(snapshot.report?.total.inboundBitrate == 8_066_000, "Total is transport-provided")
            expect(snapshot.report?.audio.lossRate == 0.01, "Media loss stays separate")
            expect(snapshot.report?.srtpActive == true && snapshot.connectionMode == "TURN 中继", "Actual security and route")
            report.mode = .direct
            report.srtpActive = false
            state.receive(report, at: 11)
            expect(state.snapshot(at: 11).connectionMode == "P2P 直连", "Route updates")
            expect(state.snapshot(at: 11).report?.srtpActive == false, "Security is not inferred from connection")
        } else if test == "expiry" {
            state.receive(report, at: 10)
            expect(state.snapshot(at: 12.99).report != nil, "One missed report does not flicker")
            let expired = state.snapshot(at: 13)
            expect(expired.report == nil && expired.rttMilliseconds == nil, "Stalled reports expire")
            expect(expired.connectionMode == "—", "Stale route expires")
            state = RemoteNetworkStatistics()
            expect(state.snapshot(at: 13).report == nil, "Reconnect clears prior peer")
        } else if test == "rtt" {
            state.receive(report, at: 10)
            report.rttMilliseconds = 100
            state.receive(report, at: 11)
            expect(near(state.snapshot(at: 11).rttMilliseconds, 40), "Desktop EWMA alpha is 0.25")
            for invalid in [-1.0, Double.nan, Double.infinity, 2_001] {
                report.rttMilliseconds = invalid
                state.receive(report, at: 12)
                expect(near(state.snapshot(at: 12).rttMilliseconds, 40), "Invalid RTT cannot contaminate average")
            }
            expect(state.snapshot(at: 14).rttMilliseconds == nil, "Invalid reports cannot keep RTT alive")
            expect(state.snapshot(at: 14).report != nil, "Traffic and RTT expire independently")
            report.rttMilliseconds = 0
            state.receive(report, at: 15)
            expect(near(state.snapshot(at: 15).rttMilliseconds, 0), "Zero RTT is valid; expired average is replaced")
        } else if test == "frames" {
            state.recordSubmittedFrame(id: 1, captureUptime: 9.9, at: 10)
            state.recordSubmittedFrame(id: 1, captureUptime: 9.9, at: 10.1)
            state.recordSubmittedFrame(id: 2, captureUptime: 10, at: 10.2)
            let snapshot = state.snapshot(at: 10.3)
            expect(snapshot.framesPerSecond == 2, "Redrawing a frame is not FPS")
            expect(near(snapshot.videoLatencyMilliseconds, 150), "Average includes time through display submission")
            expect(state.snapshot(at: 11).framesPerSecond == 1, "One-second rolling window")
            expect(state.snapshot(at: 11.3).framesPerSecond == 0, "Idle video must reach zero FPS")
            expect(state.snapshot(at: 11.3).videoLatencyMilliseconds == nil, "Idle latency expires")
        } else if test == "video_reset" {
            state.receive(report, at: 10)
            state.recordSubmittedFrame(id: 1, captureUptime: 9.9, at: 10)
            state.resetVideo()
            state.recordSubmittedFrame(id: 1, captureUptime: 9.9, at: 10.1)
            expect(state.snapshot(at: 10.1).framesPerSecond == 0, "Foreground cached frame is not counted again")
            expect(state.snapshot(at: 10.1).report != nil, "Switching displays preserves transport stats")
            state.recordSubmittedFrame(id: 2, captureUptime: 0, at: 10.2)
            state.recordSubmittedFrame(id: 3, captureUptime: 12, at: 10.3)
            state.recordSubmittedFrame(id: 4, captureUptime: 1, at: 10.4)
            state.recordSubmittedFrame(id: 5, captureUptime: .nan, at: 10.5)
            expect(state.snapshot(at: 10.5).framesPerSecond == 4, "Uncalibrated frames still count for FPS")
            expect(state.snapshot(at: 10.5).videoLatencyMilliseconds == nil, "Invalid capture estimates are unavailable")
            state.recordSubmittedFrame(id: 6, captureUptime: 10.5, at: 10.6)
            expect(near(state.snapshot(at: 10.6).videoLatencyMilliseconds, 100), "Calibration recovers without invalid samples")
        } else if test == "display_cadence" {
            state.receive(report, at: 10)
            state.recordSubmittedFrame(id: 1, captureUptime: 9.9, at: 10)
            let first = state.displaySnapshot(at: 10)
            expect(near(first.videoLatencyMilliseconds, 100), "Initial display sample")
            for frame in 1...59 {
                let now = 10 + Double(frame) / 60
                let delay = frame.isMultiple(of: 2) ? 0.3 : 0.2
                state.recordSubmittedFrame(id: UInt64(frame + 1), captureUptime: now - delay, at: now)
                report.rttMilliseconds = 100
                report.total.inboundBitrate = 9_000_000
                state.receive(report, at: now)
                let displayed = state.displaySnapshot(at: now)
                expect(near(displayed.videoLatencyMilliseconds, 100), "60 FPS callbacks cannot refresh the displayed delay")
                expect(displayed.framesPerSecond == first.framesPerSecond, "FPS is sampled with delay")
                expect(near(displayed.rttMilliseconds, 20), "RTT is frozen between ticks")
                expect(displayed.report?.total.inboundBitrate == 8_066_000, "Reports are sampled with video stats")
            }
            let next = state.displaySnapshot(at: 11)
            expect(next.framesPerSecond == 59, "The next tick samples all recent frames")
            expect((next.videoLatencyMilliseconds ?? 0) > 200, "Delay updates at the one-second boundary")
            expect(next.report?.total.inboundBitrate == 9_000_000, "The next tick sees current traffic")
            let idle = state.displaySnapshot(at: 12)
            expect(idle.framesPerSecond == 0 && idle.videoLatencyMilliseconds == nil, "Timer ticks expire idle video without new frames")
            let expired = state.displaySnapshot(at: 14)
            expect(expired.report == nil && expired.rttMilliseconds == nil, "Timer ticks expire missing reports")
        } else if test == "display_reset" {
            state.receive(report, at: 10)
            state.recordSubmittedFrame(id: 1, captureUptime: 9.9, at: 10)
            _ = state.displaySnapshot(at: 10)
            state.resetVideo()
            let switched = state.displaySnapshot(at: 10.1)
            expect(switched.framesPerSecond == 0 && switched.videoLatencyMilliseconds == nil, "Switching displays clears cached video immediately")
            expect(switched.report != nil, "Video reset keeps transport state")
            state = RemoteNetworkStatistics()
            let reconnected = state.displaySnapshot(at: 10.2)
            expect(reconnected.report == nil && reconnected.videoLatencyMilliseconds == nil, "A new session cannot reuse the old display cache")
        } else if test == "format" {
            expect(RemoteNetworkSnapshot.bitrate(nil) == "—", "Unavailable is not zero")
            expect(RemoteNetworkSnapshot.bitrate(0) == "0 bps", "Zero traffic is valid")
            expect(RemoteNetworkSnapshot.bitrate(64_000) == "64 Kbps", "Bitrate units")
            expect(RemoteNetworkSnapshot.bitrate(8_000_000) == "8.0 Mbps", "Mbps units")
            expect(RemoteNetworkSnapshot.latency(0) == "<1 ms", "Sub-millisecond RTT")
            expect(RemoteNetworkSnapshot.latency(nil) == "—", "Missing calibration")
            expect(RemoteNetworkSnapshot.lossRate(0.005) == "0.5%", "Small losses remain visible")
            expect(RemoteNetworkSnapshot.lossRate(1.2) == "120.0%", "Desktop aggregate is not silently clamped")
            expect(RemoteNetworkSnapshot.lossRate(.nan) == "—", "Nonfinite loss is unavailable")
        } else {
            fatalError("Unknown test")
        }
    }
}
''')
        source = Path(__file__).resolve().parents[1] / "CrossDeskMobile/RemoteNetworkStatistics.swift"
        cls.binary = root / "network-statistics-tests"
        subprocess.run(["xcrun", "swiftc", str(source), str(harness), "-o", str(cls.binary)],
                       check=True, capture_output=True, text=True)

    def test_channel_and_security_reports(self):
        self.run_case("report")

    def test_missing_reports_and_reconnect(self):
        self.run_case("expiry")

    def test_rtt_smoothing_and_recovery(self):
        self.run_case("rtt")

    def test_frame_submission_and_idle_video(self):
        self.run_case("frames")

    def test_display_switch_and_invalid_capture_time(self):
        self.run_case("video_reset")

    def test_units_and_unavailable_values(self):
        self.run_case("format")

    def test_display_refresh_is_independent_of_video_frames(self):
        self.run_case("display_cadence")

    def test_display_cache_resets_with_video_and_session(self):
        self.run_case("display_reset")

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
