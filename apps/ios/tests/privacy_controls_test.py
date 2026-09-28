"""Run the production privacy preferences and preview file store on macOS."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "requires the Apple Swift runtime")
class PrivacyControlsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="crossdesk-privacy-tests-")
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        harness = root / "PrivacyTests.swift"
        harness.write_text(r'''
import Foundation

func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}

func wait(_ semaphore: DispatchSemaphore) {
    expect(semaphore.wait(timeout: .now() + 10) == .success, "Operation timed out")
}

@main
enum PrivacyTests {
    static func main() throws {
        let test = CommandLine.arguments[1]
        if test == "consent" || test == "upgrade" {
            let suite = "crossdesk.privacy.tests.\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defer { defaults.removePersistentDomain(forName: suite) }
            if test == "upgrade" {
                // Existing identity/history must not be interpreted as consent.
                defaults.set("test-identity", forKey: "crossdesk.mobile.identity")
                defaults.set(Data("legacy history".utf8),
                             forKey: "crossdesk.mobile.recent-connections.v1")
            }
            let preferences = PrivacyPreferences(defaults: defaults)
            expect(!preferences.hasMadeNetworkChoice, "New/legacy install has no choice")
            expect(!preferences.hasNetworkConsent, "Must not start networking")
            expect(!preferences.savesThumbnails, "Saving must default to off")
            preferences.setSavesThumbnails(true)
            expect(!preferences.savesThumbnails, "Cannot enable captures without consent")
            preferences.setNetworkConsent(false)
            let declined = PrivacyPreferences(defaults: defaults)
            expect(declined.hasMadeNetworkChoice, "Decline must persist across launches")
            expect(!declined.hasNetworkConsent, "Decline must not allow networking")
            declined.setNetworkConsent(true)
            expect(!declined.savesThumbnails, "Network consent is not screenshot consent")
            declined.setSavesThumbnails(true)
            let relaunched = PrivacyPreferences(defaults: defaults)
            expect(relaunched.hasNetworkConsent && relaunched.savesThumbnails,
                   "Explicit choices must survive relaunch")
            relaunched.setNetworkConsent(false)
            expect(!relaunched.hasNetworkConsent && !relaunched.savesThumbnails,
                   "Withdrawal revokes both permissions")
            relaunched.setNetworkConsent(true)
            expect(!relaunched.savesThumbnails, "Re-consent must not re-enable screenshots")
        } else {
            let directory = FileManager.default.temporaryDirectory
                .appendingPathComponent("crossdesk-thumbnails-\(UUID().uuidString)")
            defer { try? FileManager.default.removeItem(at: directory) }
            let store = ThumbnailFileStore(directory: directory)
            let started = DispatchSemaphore(value: 0)
            let finishCapture = DispatchSemaphore(value: 0)
            let cleared = DispatchSemaphore(value: 0)
            store.save(fileName: "remote.jpg", makeData: {
                started.signal()
                wait(finishCapture)
                return Data("sensitive frame".utf8)
            }) { fileName in
                expect(fileName == "remote.jpg", "Capture must exercise the real write path")
            }
            wait(started)
            if test == "clear" {
                // Withdrawal/clear arrives while image conversion is still running.
                store.removeAll { success in
                    expect(success, "Clear must succeed")
                    cleared.signal()
                }
            } else {
                // Removing a recent connection must also win over its pending capture.
                store.remove(fileName: "remote.jpg")
                store.save(fileName: "barrier.jpg", makeData: { Data([1]) }) { _ in
                    cleared.signal()
                }
            }
            finishCapture.signal()
            wait(cleared)
            expect(!FileManager.default.fileExists(atPath: store.url(for: "remote.jpg").path),
                   "A pending write must not resurrect a deleted preview")
            // A later explicitly permitted capture can still be saved.
            let saved = DispatchSemaphore(value: 0)
            store.save(fileName: "new.jpg", makeData: { Data("new frame".utf8) }) { name in
                expect(name == "new.jpg", "New captures must work after clearing")
                saved.signal()
            }
            wait(saved)
            let data = try Data(contentsOf: store.url(for: "new.jpg"))
            expect(data == Data("new frame".utf8), "Saved preview must be intact")
            let values = try directory.resourceValues(forKeys: [.isExcludedFromBackupKey])
            expect(values.isExcludedFromBackup == true, "Previews must be excluded from backup")
        }
    }
}
''')
        source = Path(__file__).resolve().parents[1] / "CrossDeskMobile"
        cls.binary = root / "privacy-tests"
        subprocess.run([
            "xcrun", "swiftc", "-swift-version", "5",
            str(source / "PrivacyPreferences.swift"),
            str(source / "ThumbnailFileStore.swift"), str(harness),
            "-o", str(cls.binary),
        ], check=True, capture_output=True, text=True)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True,
                                text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_consent_persistence_and_withdrawal(self):
        self.run_case("consent")

    def test_upgrade_requires_fresh_choices(self):
        self.run_case("upgrade")

    def test_clear_wins_over_in_flight_capture(self):
        self.run_case("clear")

    def test_remove_connection_wins_over_in_flight_capture(self):
        self.run_case("remove")


if __name__ == "__main__":
    unittest.main()
