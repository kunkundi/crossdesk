"""Check Xmake discovery and the minimum supported version before native builds."""

import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch


source = Path(__file__).resolve().parents[1] / "scripts/build_native.py"
spec = importlib.util.spec_from_file_location("build_native", source)
build_native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_native)


class XmakeSelectionTest(unittest.TestCase):
    def setUp(self):
        for patcher in (
            patch.dict(os.environ, {}, clear=True),
            patch.object(build_native.shutil, "which", return_value="/tools/xmake"),
            patch.object(build_native.subprocess, "check_output", return_value="xmake v3.0.0\n"),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_path_tool_accepts_minimum_and_newer_versions(self):
        for output in ("xmake v3.0.0", "xmake v3.0.1\n", "xmake v3.0.9\n", "xmake v3.0.99\n",
                       "xmake v3.1.0\n", "xmake v3.1.1\n", "xmake v3.1.2\n", "xmake v3.1.10, A build utility\n",
                       "xmake v3.2.0\n", "xmake v3.10.0\n", "xmake v4.0.0\n",
                       "\x1b[1mxmake v3.0.0+HEAD.3ba37a0d4, A build utility\x1b[0m\n"):
            with self.subTest(output=output):
                build_native.subprocess.check_output.return_value = output
                self.assertEqual(build_native.find_xmake(), "/tools/xmake")

    def test_explicit_tool_takes_precedence_over_path(self):
        os.environ["XMAKE_BIN"] = "/custom/xmake"
        self.assertEqual(build_native.find_xmake(), "/custom/xmake")
        build_native.shutil.which.assert_not_called()
        build_native.subprocess.check_output.assert_called_once_with(
            ["/custom/xmake", "--version"], text=True)

    def test_missing_tool_reports_minimum_requirement(self):
        build_native.shutil.which.return_value = None
        with self.assertRaisesRegex(SystemExit, "Xmake 3.0.0 or newer is required"):
            build_native.find_xmake()
        build_native.subprocess.check_output.assert_not_called()

    def test_older_or_unrecognized_versions_are_rejected(self):
        for output in ("xmake v2.9.9\n", "xmake v2.99.99\n",
                       "xmake version unknown\n", "xmake v3.0\n", "xmake v3.0.0invalid\n", ""):
            with self.subTest(output=output):
                build_native.subprocess.check_output.return_value = output
                with self.assertRaisesRegex(SystemExit, "Use Xmake 3.0.0 or newer"):
                    build_native.find_xmake()


if __name__ == "__main__":
    unittest.main()
