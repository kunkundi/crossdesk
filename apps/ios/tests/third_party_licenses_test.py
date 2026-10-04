"""Regression coverage for notice validation and build-specific source links."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


def load_script(name):
    path = Path(__file__).resolve().parents[1] / "scripts" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


licenses = load_script("generate_third_party_licenses")
metadata = load_script("prepare_source_metadata")


class LicenseValidationTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="crossdesk-ios-licenses-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.mini = self.root / "deps/submodules/minirtc"
        self.mini.mkdir(parents=True)
        self.notice = self.mini / "LICENSE"
        self.notice.write_text("Reviewed license text\n")
        self.recipe = self.mini / "thirdparty/opus/xmake.lua"
        self.recipe.parent.mkdir(parents=True)
        self.recipe.write_text('package("opus")\n')
        self.source = self.mini / "transport.cpp"
        self.source.write_text("int transport() { return 1; }\n")
        self.git("init", "-q")
        self.git("add", ".")
        self.git("commit", "-qm", "Reviewed source")
        self.reviewed_revision = self.git("rev-parse", "HEAD")

        self.bundle = self.root / "ThirdPartyLicenses.json"
        self.bundle.write_text('{"schemaVersion": 1, "components": []}\n')
        self.archive = self.root / "packages/o/opus/1.0/abcd/lib/libopus.a"
        self.archive.parent.mkdir(parents=True)
        self.archive.write_bytes(b"reviewed archive")
        self.inputs = self.root / "inputs.sha256"
        self.inputs.write_text(f"{licenses.digest(self.archive.read_bytes())}  {self.archive}\n")
        self.target = self.root / "target-info.txt"
        self.target.write_text(str(self.root / "packages/a/asio/1.0/abcd/include") + "\n")
        self.lock = {
            "bundle_sha256": licenses.digest(self.bundle.read_bytes()),
            "toolchain": {"xmake_min_version": "3.1.1"},
            "components": [
                {
                    "id": "minirtc", "version": self.reviewed_revision,
                    "source": {"kind": "repository", "url": metadata.MINIRTC_URL,
                               "revision": self.reviewed_revision},
                    "documents": [{"path": str(self.notice.relative_to(self.root)),
                                   "sha256": licenses.digest(self.notice.read_bytes())}],
                },
                {
                    "id": "opus", "package": "opus", "version": "1.0",
                    "source": {"kind": "archive", "url": "https://example.com/opus.tar.gz"},
                    "recipe_path": str(self.recipe.relative_to(self.root)),
                    "recipe_sha256": licenses.digest(self.recipe.read_bytes()),
                },
                {"id": "asio", "package": "asio", "version": "1.0",
                 "source": {"kind": "archive", "url": "https://example.com/asio.tar.gz"}},
            ],
        }
        for name, value in (("REPO", self.root), ("OUTPUT", self.bundle)):
            patcher = patch.object(licenses, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.mini), "-c", "user.name=Test",
             "-c", "user.email=test@example.com", "-c", "commit.gpgsign=false", *args],
            text=True).strip()

    def verify(self, preflight=False, **kwargs):
        licenses.verify(self.lock, None if preflight else self.inputs,
                        None if preflight else self.target, **kwargs)

    def test_source_only_commit_keeps_reviewed_catalog_and_uses_actual_links(self):
        self.source.write_text("int transport() { return 2; }\n")
        self.git("add", ".")
        self.git("commit", "-qm", "Change transport implementation")
        current = self.git("rev-parse", "HEAD")
        self.assertNotEqual(current, self.reviewed_revision)
        original_lock = copy.deepcopy(self.lock)
        original_bundle = self.bundle.read_bytes()
        self.verify(preflight=True)
        self.verify()
        sources = metadata.component_sources(self.lock, current)
        self.assertEqual(sources["minirtc"]["version"], current)
        self.assertEqual(sources["minirtc"]["sourceURL"], f"{metadata.MINIRTC_URL}/tree/{current}")
        self.assertEqual(sources["opus"]["buildSourceURL"],
                         f"{metadata.MINIRTC_URL}/tree/{current}/thirdparty/opus")
        self.assertEqual(self.lock, original_lock)
        self.assertEqual(self.bundle.read_bytes(), original_bundle)

    def test_changed_license_is_rejected_before_compilation(self):
        self.notice.write_text("Changed license text\n")
        with self.assertRaisesRegex(ValueError, "Source checksum mismatch: .*LICENSE"):
            self.verify(preflight=True)

    def test_changed_recipe_is_rejected_before_compilation(self):
        self.recipe.write_text('package("different-opus")\n')
        with self.assertRaisesRegex(ValueError, "Source checksum mismatch: .*xmake.lua"):
            self.verify(preflight=True)

    def test_changed_bundle_is_rejected_before_compilation(self):
        self.bundle.write_text("{}\n")
        with self.assertRaisesRegex(ValueError, "Source checksum mismatch:"):
            self.verify(preflight=True)

    def test_unreviewed_header_only_dependency_is_rejected(self):
        self.target.write_text(str(self.root / "packages/a/asio/2.0/abcd/include") + "\n")
        with self.assertRaisesRegex(ValueError, "Unreviewed iOS dependency: asio 2.0"):
            self.verify()

    def test_missing_dependency_is_rejected(self):
        self.target.write_text("")
        with self.assertRaisesRegex(ValueError, "License/build dependency mismatch; missing:.*asio"):
            self.verify()

    def test_simulator_cache_keeps_dependency_and_archive_validation(self):
        (self.root / "packages").rename(self.root / "packages-simulator")
        self.archive = Path(str(self.archive).replace("/packages/", "/packages-simulator/"))
        self.inputs.write_text(self.inputs.read_text().replace("/packages/", "/packages-simulator/"))
        self.target.write_text(self.target.read_text().replace("/packages/", "/packages-simulator/"))
        self.verify()
        valid_target = self.target.read_text()
        self.target.write_text(valid_target.replace("/asio/1.0/", "/asio/2.0/"))
        with self.assertRaisesRegex(ValueError, "Unreviewed iOS dependency: asio 2.0"):
            self.verify()
        self.target.write_text(valid_target)
        self.archive.write_bytes(b"changed simulator archive")
        with self.assertRaisesRegex(ValueError, "Source checksum mismatch: .*libopus.a"):
            self.verify()

    def test_changed_native_archive_is_rejected(self):
        self.archive.write_bytes(b"changed archive")
        with self.assertRaisesRegex(ValueError, "Source checksum mismatch: .*libopus.a"):
            self.verify()

    def test_minimum_and_newer_xmake_versions_are_accepted(self):
        version = self.root / "xmake-version.txt"
        for output in ("xmake v3.1.1", "xmake v3.1.2\n", "xmake v3.1.10, A build utility\n",
                       "xmake v3.2.0\n", "xmake v3.10.0\n", "xmake v4.0.0\n",
                       "\x1b[1mxmake v3.1.1+HEAD.3ba37a0d4, A build utility\x1b[0m\n"):
            with self.subTest(output=output):
                version.write_text(output)
                self.verify(xmake_version=version)

    def test_older_or_unrecognized_xmake_versions_are_rejected(self):
        version = self.root / "xmake-version.txt"
        for output in ("xmake v3.1.0\n", "xmake v3.0.99\n", "xmake v2.99.99\n",
                       "xmake version unknown\n", "xmake v3.1\n", "xmake v3.1.1invalid\n", ""):
            with self.subTest(output=output):
                version.write_text(output)
                with self.assertRaisesRegex(ValueError, "Xmake 3.1.1 or newer is required"):
                    self.verify(xmake_version=version)


class ComponentSourceTest(unittest.TestCase):
    def test_all_minirtc_components_and_custom_recipes_follow_build_revision(self):
        lock_path = Path(__file__).resolve().parents[1] / "licenses/sources.json"
        lock = json.loads(lock_path.read_text())
        reviewed = copy.deepcopy(lock)
        revision = "a" * 40
        base = f"{metadata.MINIRTC_URL}/tree/{revision}"
        sources = metadata.component_sources(lock, revision)
        for name in ("minirtc", "inih", "webrtc"):
            self.assertEqual(sources[name], {"version": revision, "sourceURL": base})
        self.assertEqual(sources["openssl3"], {"buildSourceURL": base + "/thirdparty/openssl"})
        self.assertNotIn("asio", sources)
        self.assertNotIn("crossdesk", sources)
        self.assertEqual(lock, reviewed)


if __name__ == "__main__":
    unittest.main()
