"""Check platform persistence without losing existing recent connections."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "requires the Apple Swift runtime")
class RecentConnectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="crossdesk-recent-connections-")
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        harness = root / "RecentConnectionTests.swift"
        harness.write_text(r'''
import Foundation

func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}

@main enum RecentConnectionTests {
    static func main() throws {
        let test = CommandLine.arguments[1]
        let decoder = JSONDecoder()
        let legacy = #"{"remoteID":"123456","displayName":"Work PC","lastConnectedAt":100,"remembersPassword":true,"thumbnailFileName":"123456.jpg"}"#
        if test == "legacy" {
            for field in ["", #", "platform":null"#, #", "platform":23"#,
                          #", "platform":"future-os""#] {
                let record = String(legacy.dropLast()) + field + "}"
                let connections = try decoder.decode([RecentConnection].self,
                    from: Data("[\(record)]".utf8))
                expect(connections.count == 1, "Old history must not be discarded")
                let connection = connections[0]
                expect(connection.platform == .unknown, "Missing or invalid platform is unknown")
                expect(connection.platform.displayName == "未知", "Unknown has a readable placeholder")
                expect(connection.remoteID == "123456" && connection.displayName == "Work PC",
                       "Existing identity and name survive migration")
                expect(connection.lastConnectedAt == Date(timeIntervalSinceReferenceDate: 100) &&
                       connection.remembersPassword && connection.thumbnailFileName == "123456.jpg",
                       "Migration preserves dates, saved-password flags and previews")
            }
        } else if test == "round_trip" {
            let platforms: [RemoteHostPlatform] = [.windows, .linux, .macos, .ios, .unknown]
            let connections = platforms.enumerated().map { index, platform in
                RecentConnection(remoteID: String(index), displayName: "Host \(index)",
                    lastConnectedAt: Date(timeIntervalSinceReferenceDate: Double(index)),
                    remembersPassword: false, thumbnailFileName: nil, platform: platform)
            }
            let data = try JSONEncoder().encode(connections)
            let reloaded = try decoder.decode([RecentConnection].self, from: data)
            expect(reloaded == connections, "Platforms persist per device without requiring an image")
        } else if test == "host_info" {
            var connection = try decoder.decode(RecentConnection.self, from: Data(legacy.utf8))
            connection.updateHostInfo(name: "  ", platform: .windows)
            expect(connection.displayName == "Work PC" && connection.platform == .windows,
                   "An empty host name must not prevent a platform update")
            connection.updateHostInfo(name: " Linux workstation ", platform: .linux)
            expect(connection.displayName == "Linux workstation" && connection.platform == .linux,
                   "New host information replaces the cached platform")
            connection.updateHostInfo(name: "", platform: .unknown)
            let data = try JSONEncoder().encode(connection)
            let reloaded = try decoder.decode(RecentConnection.self, from: data)
            expect(reloaded.platform == .unknown && reloaded.thumbnailFileName == "123456.jpg",
                   "Legacy peers replace the old platform without removing a saved preview")
        } else {
            fatalError("Unknown test")
        }
    }
}
''')
        source = Path(__file__).resolve().parents[1] / "CrossDeskMobile/RecentConnection.swift"
        cls.binary = root / "recent-connection-tests"
        subprocess.run(["xcrun", "swiftc", str(source), str(harness), "-o", str(cls.binary)],
                       check=True, capture_output=True, text=True)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

    def test_existing_history_survives_platform_migration(self):
        self.run_case("legacy")

    def test_platforms_persist_without_saved_previews(self):
        self.run_case("round_trip")

    def test_received_host_info_replaces_cached_platform(self):
        self.run_case("host_info")


if __name__ == "__main__":
    unittest.main()
