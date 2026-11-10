"""Run the production event-driven update checker with a deterministic transport."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "requires the Apple Swift runtime")
class AppUpdateCheckerTest(unittest.TestCase):
    def test_visit_session_end_failures_and_lifecycle(self):
        source = Path(__file__).resolve().parents[1] / "CrossDeskMobile/AppUpdateChecker.swift"
        with tempfile.TemporaryDirectory(prefix="crossdesk-app-update-tests-") as directory:
            harness = Path(directory) / "Tests.swift"
            harness.write_text(r'''
import Foundation

func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}
@MainActor
final class Fixture {
    var replies: [(Data?) -> Void] = []
    var cancellations = 0
    var time = Date(timeIntervalSince1970: 0)
    lazy var checker = AppUpdateChecker(currentVersion: "0.0.1",
        bundleIdentifier: "com.chunkdash.crossdesk", appStoreID: "6828859211", systemVersion: "17.0",
        fetch: { completion in
        self.replies.append(completion)
        return { self.cancellations += 1 }
    }, now: { self.time })
    func reply(_ value: String?) {
        replies.last!(value.map { version in
            try! JSONSerialization.data(withJSONObject: ["resultCount": 1, "results": [[
                "wrapperType": "software", "bundleId": "com.chunkdash.crossdesk",
                "trackId": 6828859211, "version": version, "minimumOsVersion": "16.0"
            ]]])
        })
    }
}
@main
enum Tests {
    @MainActor static func main() {
        let f = Fixture()
        f.checker.checkNow()
        expect(f.replies.isEmpty, "No checks without consent and foreground")
        f.checker.setEnabled(true)
        f.checker.checkNow()
        f.checker.setEnabled(true)
        expect(f.replies.count == 1, "Deduplicate concurrent checks")
        f.reply("0.0.2")
        expect(f.checker.updateAvailable && f.checker.status == .available, "Show badge")
        f.checker.setEnabled(true)
        expect(f.replies.count == 1, "Duplicate active callbacks do not recheck after completion")
        f.checker.setEnabled(false)
        f.time.addTimeInterval(6 * 60 * 60)
        f.checker.setEnabled(true)
        expect(f.replies.count == 2, "A new visit checks after the automatic cooldown")
        f.reply(nil)
        expect(f.checker.status == .failed && f.checker.updateAvailable,
               "Failed checks retain discovered update")
        f.checker.setEnabled(true)
        expect(f.replies.count == 2, "A failure does not restart the check")
        // The About action explicitly bypasses the automatic cooldown.
        f.checker.checkNow()
        f.checker.checkNow()
        expect(f.replies.count == 3, "Explicit events check again without a cooldown and deduplicate")
        f.reply("0.0.1")
        expect(!f.checker.updateAvailable && f.checker.status == .upToDate,
               "Successful current-version check clears badge")
        f.checker.checkNow()
        let stale = f.replies.last!
        f.checker.setEnabled(false)
        expect(f.cancellations == 1, "Background/revocation cancels request")
        stale(Data("9.0.0".utf8))
        expect(!f.checker.updateAvailable, "Ignore cancelled response")
        f.time.addTimeInterval(6 * 60 * 60)
        f.checker.setEnabled(true)
        stale(Data("9.0.0".utf8))
        expect(f.checker.status == .checking, "Ignore old response during new request")
        f.reply("0.0.2")
        f.checker.setEnabled(false)
        let beforeForeground = f.replies.count
        f.checker.checkNow()
        expect(f.replies.count == beforeForeground, "Session endings in background do not check")
        f.time.addTimeInterval(6 * 60 * 60)
        f.checker.setEnabled(true)
        expect(f.replies.count == beforeForeground + 1, "Check once on return from background")
        expect(f.checker.updateAvailable, "Retain badge while rechecking")
        f.checker.setEnabled(false)
    }
}
''')
            binary = Path(directory) / "app-update-tests"
            subprocess.run(["xcrun", "swiftc", str(source), str(harness), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
