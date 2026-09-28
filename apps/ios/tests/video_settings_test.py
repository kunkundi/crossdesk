"""Exercise the production live-video settings state with the Apple Swift runtime."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "requires the Apple Swift runtime")
class VideoSettingsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="crossdesk-video-settings-tests-")
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        harness = root / "VideoSettingsTests.swift"
        harness.write_text(r'''
func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}

@main
enum VideoSettingsTests {
    static func main() {
        let test = CommandLine.arguments[1]
        let initial = RemoteVideoSettings(preference: 0)
        let first = RemoteVideoSettings(quality: 1, frameRate: 30, preference: 2)
        let second = RemoteVideoSettings(quality: 0, frameRate: 60, preference: 1)
        var state = RemoteVideoSettingsState(preference: 0)
        expect(state.selection == initial, "Session starts with the saved preference")
        expect(state.begin(first) == nil, "Old hosts cannot receive live settings")
        state.setSupported(true)

        if test == "invalid" {
            for quality in -1...3 {
                for frameRate in [0, 24, 30, 60, 120] {
                    for preference in -1...3 {
                        let settings = RemoteVideoSettings(quality: quality,
                            frameRate: frameRate, preference: preference)
                        let valid = (0...2).contains(quality) &&
                            [30, 60].contains(frameRate) && (0...2).contains(preference)
                        expect((state.begin(settings) != nil) == valid,
                               "Only desktop protocol values can be submitted")
                    }
                }
            }
            return
        }

        let firstID = state.begin(first)!
        expect(state.pending && state.selection == first, "Selection is optimistic")
        expect(state.applied == initial, "Submission is not confirmation")

        if test == "accepted" {
            state.receive(first, requestID: firstID, accepted: true)
            expect(!state.pending && !state.failed && state.selection == first,
                   "An accepted response commits the selection")
            state.expire(requestID: firstID)
            expect(!state.failed, "A completed request cannot time out")
        } else if test == "rejected" {
            state.receive(first, requestID: firstID, accepted: false)
            expect(state.failed && !state.pending && state.selection == initial,
                   "Rejection rolls back the optimistic selection")
            expect(state.begin(first) != nil && !state.failed && state.pending,
                   "A failed selection can be retried")
        } else if test == "superseded" {
            let secondID = state.begin(second)!
            state.receive(first, requestID: firstID, accepted: true)
            state.expire(requestID: firstID)
            expect(state.selection == second && state.pending,
                   "An old reply or timeout cannot overwrite a newer selection")
            state.receive(second, requestID: secondID, accepted: false)
            expect(state.selection == first && state.failed,
                   "Rollback uses the most recently acknowledged settings")
        } else if test == "out_of_order" {
            let secondID = state.begin(second)!
            state.receive(second, requestID: secondID, accepted: true)
            state.receive(first, requestID: firstID, accepted: true)
            expect(state.selection == second && state.applied == second && !state.failed,
                   "A stale success cannot revert a newer success")
            state.receive(first, requestID: secondID + 1, accepted: true)
            expect(state.applied == second, "Unsent request IDs are ignored")
        } else if test == "timeout" {
            state.expire(requestID: firstID)
            expect(state.failed && !state.pending && state.selection == initial,
                   "Timeout restores the last applied settings")
            state.receive(first, requestID: firstID, accepted: true)
            expect(state.selection == first && !state.failed,
                   "A late confirmation reconciles the actual remote state")
        } else if test == "unsupported" {
            state.setSupported(false)
            state.receive(first, requestID: firstID, accepted: true)
            state.expire(requestID: firstID)
            expect(!state.pending && !state.failed && state.selection == initial,
                   "Losing capability clears pending feedback and ignores replies")
            expect(state.begin(second) == nil, "Unavailable controls cannot send")
            state = RemoteVideoSettingsState(preference: 2)
            expect(!state.supported && state.requestID == 0 &&
                   state.selection == RemoteVideoSettings(preference: 2),
                   "A new session resets capability, request IDs and settings")
        } else {
            fatalError("Unknown test")
        }
    }
}
''')
        source = Path(__file__).resolve().parents[1] / "CrossDeskMobile/RemoteVideoSettings.swift"
        cls.binary = root / "video-settings-tests"
        subprocess.run(["xcrun", "swiftc", str(source), str(harness),
                        "-o", str(cls.binary)], check=True, capture_output=True, text=True)

    def test_protocol_values(self):
        self.run_case("invalid")

    def test_acceptance(self):
        self.run_case("accepted")

    def test_rejection_and_retry(self):
        self.run_case("rejected")

    def test_rapid_changes(self):
        self.run_case("superseded")

    def test_out_of_order_responses(self):
        self.run_case("out_of_order")

    def test_timeout_and_late_confirmation(self):
        self.run_case("timeout")

    def test_unsupported_and_new_session(self):
        self.run_case("unsupported")

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
