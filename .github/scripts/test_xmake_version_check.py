"""Exercise CI Xmake selection with isolated executable and Actions-output mocks."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "actions/ensure-xmake/check-version.sh"
BASH = shutil.which("bash")


@unittest.skipUnless(BASH, "Bash is required to exercise the CI version check")
class XmakeVersionCheckTest(unittest.TestCase):
    def check(self, version_output=None, *, minimum="3.1.1", status=0, arguments=()):
        with tempfile.TemporaryDirectory(prefix="crossdesk-xmake-check-") as directory:
            root = Path(directory)
            output_file = root / "github-output"
            output_file.write_text("existing-output=preserved\n", encoding="utf-8")
            arguments_file = root / "arguments"
            if version_output is not None:
                executable = root / "xmake"
                executable.write_text(
                    "#!/bin/sh\n"
                    'printf "%s\\n" "$@" > "$XMAKE_TEST_ARGUMENTS"\n'
                    'printf "%s" "$XMAKE_TEST_OUTPUT"\n'
                    'exit "$XMAKE_TEST_STATUS"\n',
                    encoding="utf-8",
                )
                executable.chmod(0o755)
            environment = {
                **os.environ,
                # The checker only needs shell builtins. Isolating PATH also
                # guarantees that missing-tool tests cannot find a real xmake.
                "PATH": str(root),
                "GITHUB_OUTPUT": str(output_file),
                "XMAKE_TEST_ARGUMENTS": str(arguments_file),
                "XMAKE_TEST_OUTPUT": version_output or "",
                "XMAKE_TEST_STATUS": str(status),
            }
            if minimum is None:
                environment.pop("XMAKE_MIN_VERSION", None)
            else:
                environment["XMAKE_MIN_VERSION"] = minimum
            result = subprocess.run(
                [BASH, str(SCRIPT), *arguments],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
            )
            invocation = (
                arguments_file.read_text(encoding="utf-8").splitlines()
                if arguments_file.exists() else None
            )
            self.assertEqual(outputs["existing-output"], "preserved")
            return result, outputs, invocation

    def test_keeps_minimum_and_newer_versions(self):
        for version in ("3.1.1", "3.1.2", "3.1.10", "3.2.0", "3.10.0", "4.0.0"):
            with self.subTest(version=version):
                result, outputs, invocation = self.check(f"xmake v{version}\n")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(outputs["needs-upgrade"], "false")
                self.assertEqual(outputs["detected-version"], version)
                self.assertEqual(invocation, ["--root", "--version"])
                self.assertIn("Keeping installed Xmake", result.stdout)

    def test_older_versions_require_upgrade(self):
        for version in ("3.1.0", "3.0.99", "2.99.99"):
            with self.subTest(version=version):
                result, outputs, _ = self.check(f"xmake v{version}\n")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(outputs["needs-upgrade"], "true")
                self.assertIn("Install Xmake 3.1.1 or newer", result.stdout)

    def test_components_are_compared_numerically(self):
        for installed, minimum, expected in (
            ("3.1.10", "3.1.9", "false"),
            ("3.1.9", "3.1.10", "true"),
            ("3.2.0", "3.1.99", "false"),
            ("3.1.99", "3.2.0", "true"),
            ("3.01.009", "3.1.9", "false"),
            ("3.1.999999999999999999999", "3.1.9", "false"),
        ):
            with self.subTest(installed=installed, minimum=minimum):
                result, outputs, _ = self.check(f"xmake v{installed}", minimum=minimum)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(outputs["needs-upgrade"], expected)

    def test_parses_ansi_colors_build_suffix_and_windows_line_endings(self):
        result, outputs, _ = self.check(
            "Xmake banner\r\n"
            "\x1b[1;38;5;10mxmake\x1b[0m v\x1b[1m3.1.10\x1b[0m+HEAD.abcdef, A build utility\r\n"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(outputs["needs-upgrade"], "false")
        self.assertEqual(outputs["detected-version"], "3.1.10")

    def test_missing_broken_and_unrecognizable_tools_require_upgrade(self):
        cases = (
            (None, 0),
            ("xmake v9.9.9\n", 17),
            ("", 0),
            ("xmake version unknown\n", 0),
            ("xmake v3.1\n", 0),
            ("xmake v3.1.1invalid\n", 0),
            ("xmake v3.1.1-rc.1\n", 0),
            ("otherxmake v9.9.9\n", 0),
        )
        for output, status in cases:
            with self.subTest(output=output, status=status):
                result, outputs, _ = self.check(output, status=status)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(outputs["needs-upgrade"], "true")
                self.assertEqual(outputs["detected-version"], "")
                self.assertIn("Install Xmake 3.1.1 or newer", result.stdout)

    def test_verification_fails_when_install_did_not_provide_required_version(self):
        for output, status in ((None, 0), ("xmake v3.1.0", 0), ("unknown", 0), ("xmake v3.1.1", 5)):
            with self.subTest(output=output, status=status):
                result, outputs, _ = self.check(output, status=status, arguments=("--verify",))
                self.assertEqual(result.returncode, 1)
                self.assertEqual(outputs["needs-upgrade"], "true")
                self.assertIn("Xmake verification failed", result.stderr)
                self.assertIn("3.1.1 or newer", result.stderr)

    def test_verification_accepts_newer_version(self):
        result, outputs, _ = self.check("xmake v4.0.0", arguments=("--verify",))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(outputs["needs-upgrade"], "false")

    def test_invalid_minimum_fails_before_running_xmake(self):
        for minimum in (None, "", "3.1", "v3.1.1", "3.1.1+HEAD", "3.1.1\n", "3.a.1"):
            with self.subTest(minimum=minimum):
                result, outputs, invocation = self.check("xmake v3.1.1", minimum=minimum)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn("needs-upgrade", outputs)
                self.assertIsNone(invocation)
                self.assertIn("XMAKE_MIN_VERSION", result.stderr)

    def test_unknown_arguments_fail_before_running_xmake(self):
        result, outputs, invocation = self.check("xmake v3.1.1", arguments=("--install",))
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("needs-upgrade", outputs)
        self.assertIsNone(invocation)
        self.assertIn("Usage:", result.stderr)


if __name__ == "__main__":
    unittest.main()
