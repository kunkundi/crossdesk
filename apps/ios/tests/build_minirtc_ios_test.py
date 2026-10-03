"""Exercise the native build phase with fake Xmake and real Apple archives."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "requires Apple libtool and zsh")
class NativeBuildTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="crossdesk-ios-build-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ios = self.root / "apps/ios"
        scripts = self.ios / "scripts"
        scripts.mkdir(parents=True)
        source = Path(__file__).resolve().parents[1] / "scripts/build_minirtc_ios.sh"
        self.script = scripts / source.name
        shutil.copy2(source, self.script)
        (self.root / "xmake.lua").write_text('-- fixture\n')
        (self.root / "deps/submodules/minirtc").mkdir(parents=True)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.library_dir = self.root / "packages/lib"
        self.library_dir.mkdir(parents=True)
        obj = self.root / "fixture.o"
        subprocess.run(["xcrun", "clang", "-x", "c", "-c", "-", "-o", str(obj)],
                       input="int fixture(void) { return 1; }", text=True, check=True)
        self.archive = self.root / "fixture.a"
        subprocess.run(["/usr/bin/libtool", "-static", "-o", str(self.archive), str(obj)],
                       check=True, capture_output=True)
        links = re.search(r"REQUIRED_LINKS=\((.*?)\)", source.read_text(), re.S)[1].split()
        for name in links:
            shutil.copy2(self.archive, self.library_dir / f"lib{name}.a")
        self.write_tool("xcodebuild", '#!/bin/sh\ncat "$(dirname "$0")/../xcode-version"\n')
        self.write_tool("xcrun", '#!/bin/sh\necho fixture-iphoneos-sdk\n')
        (self.root / "xcode-version").write_text("Xcode fixture-1\n")
        self.write_tool("xmake", f"#!{sys.executable}\n" + r'''
import json, pathlib, shutil, sys
root = pathlib.Path(__file__).resolve().parents[1]
args = sys.argv[1:]
with (root / "calls.jsonl").open("a") as log:
    log.write(json.dumps(args) + "\n")
if args[0] == "--version":
    print("xmake fixture")
elif args[0] == "f":
    if (root / "fail-config").exists():
        sys.exit(1)
    project = pathlib.Path(args[args.index("-P") + 1])
    (project / "config.json").write_text(json.dumps(args))
elif args[0] == "b":
    project = pathlib.Path(args[args.index("-P") + 1])
    config = json.loads((project / "config.json").read_text())
    output = pathlib.Path(config[config.index("-o") + 1])
    output /= "iphoneos/arm64/" + config[config.index("-m") + 1]
    output.mkdir(parents=True, exist_ok=True)
    library = output / ("lib" + args[-1] + ".a")
    if not library.exists():
        shutil.copy2(root / "fixture.a", library)
elif args[0] == "show":
    print("    -> " + str(root / "packages/lib") + " -> package fixture")
''')
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        XMAKE_BIN=str(self.bin / "xmake"), CONFIGURATION="Release",
                        MINIRTC_ENABLE_AOM="false",
                        CURRENT_ARCH="arm64", DEVELOPER_DIR=subprocess.check_output(
                            ["xcode-select", "-p"], text=True).strip())
        self.output = self.ios / "Vendor/iphoneos/Release/libCrossDeskMiniRTC.a"

    def write_tool(self, name, contents):
        path = self.bin / name
        path.write_text(contents)
        path.chmod(0o755)

    def run_build(self, success=True):
        result = subprocess.run(["/bin/zsh", str(self.script)], env=self.env,
                                text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def config_calls(self):
        import json
        return [args for line in (self.root / "calls.jsonl").read_text().splitlines()
                if (args := json.loads(line))[0] == "f"]

    def test_noop_preserves_archives_and_manifest(self):
        self.run_build()
        manifest = self.ios / ".xmake/wire-work/xmake.lua"
        old_times = (self.output.stat().st_mtime_ns, manifest.stat().st_mtime_ns)
        self.run_build()
        self.assertEqual(old_times, (self.output.stat().st_mtime_ns, manifest.stat().st_mtime_ns))
        self.assertEqual(["-c" in args for args in self.config_calls()], [True, True, False, False])

    def test_archive_content_change_with_same_size_and_mtime_is_merged(self):
        self.run_build()
        original = self.output.read_bytes()
        library = self.library_dir / "libopus.a"
        stat = library.stat()
        data = library.read_bytes()
        # Rename an archive member without changing the archive layout or size.
        self.assertIn(b"fixture.o", data)
        library.write_bytes(data.replace(b"fixture.o", b"changed.o"))
        os.utime(library, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        self.run_build()
        self.assertNotEqual(original, self.output.read_bytes())

    def test_toolchain_and_mode_changes_clear_probes(self):
        self.run_build()
        (self.root / "xcode-version").write_text("Xcode fixture-2\n")
        self.run_build()
        self.env["CONFIGURATION"] = "Debug"
        self.run_build()
        self.assertEqual(["-c" in args for args in self.config_calls()], [True] * 6)
        self.assertTrue((self.ios / "Vendor/iphoneos/Debug/libCrossDeskMiniRTC.a").exists())

    def test_failed_configuration_does_not_commit_signature(self):
        self.run_build()
        stamp = self.ios / ".xmake/minirtc-build/ios-config-signature"
        original = stamp.read_bytes()
        (self.root / "xcode-version").write_text("Xcode fixture-2\n")
        (self.root / "fail-config").touch()
        self.run_build(success=False)
        self.assertEqual(stamp.read_bytes(), original)
        (self.root / "fail-config").unlink()
        self.run_build()
        self.assertTrue(all("-c" in args for args in self.config_calls()))

    def test_simulator_uses_separate_archives_and_switches_back_to_device(self):
        self.run_build()
        device_archive = self.output.read_bytes()
        self.env["PLATFORM_NAME"] = "iphonesimulator"
        self.run_build()
        simulator = self.ios / "Vendor/iphonesimulator/Release/libCrossDeskMiniRTC.a"
        self.assertTrue(simulator.exists())
        self.assertTrue((self.ios / ".xmake/wire-work-simulator/xmake.lua").exists())
        signature = self.ios / ".xmake/minirtc-build-simulator/ios-config-signature"
        self.assertIn("packages-simulator", signature.read_text())
        for args in self.config_calls()[-2:]:
            self.assertIn("--appledev=simulator", args)
            self.assertIn("-simulator", args[args.index("-o") + 1])
        self.env["PLATFORM_NAME"] = "iphoneos"
        self.run_build()
        self.assertEqual(self.output.read_bytes(), device_archive)
        self.assertTrue(all("--appledev=iphone" in args for args in self.config_calls()[-2:]))

    def test_aom_is_only_required_and_merged_when_enabled(self):
        self.run_build()
        manifest = Path(str(self.output) + ".inputs.sha256")
        self.assertNotIn("libaom.a", manifest.read_text())
        original = self.output.read_bytes()
        self.env["MINIRTC_ENABLE_AOM"] = "true"
        result = self.run_build(success=False)
        self.assertIn("Unable to resolve static dependency libaom.a", result.stderr)
        self.assertEqual(self.output.read_bytes(), original)
        shutil.copy2(self.archive, self.library_dir / "libaom.a")
        self.run_build()
        self.assertIn("libaom.a", manifest.read_text())
        self.env["MINIRTC_ENABLE_AOM"] = "false"
        self.run_build()
        self.assertNotIn("libaom.a", manifest.read_text())


if __name__ == "__main__":
    unittest.main()
