"""Regression coverage for independent and combined release metadata."""

import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import update_version_manifest as manifest


class ManifestTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="crossdesk-manifest-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.artifacts = self.root / "release"
        self.artifacts.mkdir()
        self.current = {
            "version": "1.6.0-20260930", "latest_version": "1.6.0-20260930",
            "releaseDate": "2026-09-30", "releaseNotes": "Desktop notes", "patch": 0,
            "downloads": {"windows-x64": {"url": "https://downloads.crossdesk.cn/existing.exe"}},
            "custom": {"keep": True},
        }

    def files(self, *names):
        for path in self.artifacts.iterdir():
            path.unlink()
        for name in names:
            (self.artifacts / name).write_bytes(b"release fixture")
        return manifest.artifact_downloads(self.artifacts)

    def merge(self, tag, downloads, current=None):
        return manifest.merge_release(current or self.current, tag, tag, "Release notes", downloads)

    def test_mobile_preserves_desktop_metadata_and_downloads(self):
        downloads = self.files("crossdesk-android-arm64-unsigned-v1.6.0-20261002.apk")
        original = copy.deepcopy(self.current)
        result = self.merge("android-v1.6.0-20261002", downloads)
        for key, value in original.items():
            if key != "downloads":
                self.assertEqual(result[key], value)
        self.assertEqual(result["downloads"]["windows-x64"], original["downloads"]["windows-x64"])
        self.assertEqual(result["downloads"]["android-arm64"]["url"],
                         "https://downloads.crossdesk.cn/crossdesk-android-arm64-unsigned-v1.6.0-20261002.apk")
        self.assertEqual(result["platforms"]["android"]["latest_version"], "1.6.0-20261002")
        self.assertEqual(self.current, original)

    def test_sequential_platform_releases_retain_each_other(self):
        for order in (("ios", "android"), ("android", "ios")):
            result = copy.deepcopy(self.current)
            for platform in order:
                version, extension = ("0.0.2", "zip") if platform == "ios" else ("1.6.0", "apk")
                downloads = self.files(f"crossdesk-{platform}-arm64-unsigned-v{version}-20261002.{extension}")
                result = self.merge(f"{platform}-v{version}-20261002", downloads, result)
            self.assertEqual(set(result["platforms"]), {"ios", "android"})
            self.assertEqual(result["version"], self.current["version"])
            self.assertTrue({"windows-x64", "ios-arm64", "android-arm64"} <= result["downloads"].keys())

    def test_combined_release_records_independent_mobile_versions(self):
        downloads = self.files("crossdesk-win-x64-v1.6.1-20261002.exe",
                               "crossdesk-win-x64-portable-v1.6.1-20261002.zip",
                               "crossdesk-ios-arm64-unsigned-v0.0.2-20261002.zip",
                               "crossdesk-android-arm64-unsigned-v1.6.0-20261002.apk")
        result = self.merge("v1.6.1-20261002", downloads)
        self.assertEqual(result["version"], "1.6.1-20261002")
        self.assertEqual(result["platforms"]["ios"]["version"], "0.0.2-20261002")
        self.assertEqual(result["platforms"]["android"]["version"], "1.6.0-20261002")

    def test_mobile_tag_must_match_artifact_and_platform(self):
        downloads = self.files("crossdesk-android-arm64-unsigned-v1.6.0-20261002.apk")
        for tag in ("android-v1.7.0-20261002", "ios-v1.6.0-20261002"):
            with self.assertRaises(ValueError):
                self.merge(tag, downloads)

    def test_invalid_current_manifest_is_rejected(self):
        downloads = self.files("crossdesk-android-arm64-unsigned-v1.6.0-20261002.apk")
        for current in ([], {}, {"downloads": []}, {"downloads": {}, "platforms": []}):
            with self.assertRaises(ValueError):
                manifest.merge_release(current, "android-v1.6.0-20261002", "Android", "", downloads)

    def test_missing_duplicate_and_invalid_artifacts_are_rejected(self):
        for names in ((), ("crossdesk-android-arm64-unsigned-v1.6.0-20260230.apk",),
                      ("crossdesk-android-arm64-unsigned-v1.6.0-20261001.apk",
                       "crossdesk-android-arm64-unsigned-v1.6.0-20261002.apk")):
            with self.assertRaises(ValueError):
                self.files(*names)

    def test_hotfix_version_and_date_formats(self):
        for value in ("v1.6.0-2-20261002", "v1.6.0-20261002-2"):
            info = manifest.version_info(value)
            self.assertEqual(info["patch"], 2)
            self.assertEqual(info["releaseDate"], "2026-10-02")

    def test_cli_preserves_literal_notes_and_release_metadata(self):
        self.files("crossdesk-ios-arm64-unsigned-v0.0.2-20261002.zip")
        notes = '中文 release notes\n"quoted" $(not-a-command) $HOME `literal`'
        release = {"tagName": "ios-v0.0.2-20261002", "name": "iOS release", "body": notes}
        (self.root / "current.json").write_text(json.dumps(self.current))
        (self.root / "release.json").write_text(json.dumps(release))
        output = self.root / "version.json"
        subprocess.run([sys.executable, str(Path(manifest.__file__)),
                        "--current", str(self.root / "current.json"), "--output", str(output),
                        "--artifacts", str(self.artifacts), "--release-json", str(self.root / "release.json")],
                       check=True, capture_output=True, text=True)
        result = json.loads(output.read_text())
        self.assertEqual(result["platforms"]["ios"]["releaseNotes"], notes)
        self.assertEqual(result["releaseNotes"], "Desktop notes")


if __name__ == "__main__":
    unittest.main()
