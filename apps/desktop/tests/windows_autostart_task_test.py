"""Validate generated Task Scheduler XML on macOS/Linux without changing startup.

Run: python3 apps/desktop/tests/windows_autostart_task_test.py
Native registration/migration coverage lives in windows_autostart_test.cpp.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[3]
NAMESPACE = {"t": "http://schemas.microsoft.com/windows/2004/02/mit/task"}


class AutostartTaskTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="crossdesk-autostart-test-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = pathlib.Path(cls.temporary.name)
        source = directory / "fixture.cpp"
        source.write_text(r'''
#include <iostream>
#include "platform/windows/autostart_task.h"
int main() {
  std::cout << crossdesk::platform::kAutostartRequiresElevation << '\n';
  const auto xml = crossdesk::platform::BuildAutostartTaskXml(
      L"S-1-5-21-123-456-789-1001", L"C:\\程序 & Tools\\CrossDesk.exe",
      L"C:\\程序 & Tools");
  for (wchar_t character : xml) std::cout << static_cast<unsigned>(character) << ' ';
}
''', encoding="utf-8")
        compiler = shutil.which("c++") or shutil.which("clang++")
        if not compiler:
            raise unittest.SkipTest("C++17 compiler is unavailable")
        cls.variants = {}
        for name, defines in {
            "installed": [], "portable": ["-DCROSSDESK_PORTABLE=1"],
            "debug": ["-DCROSSDESK_DEBUG"],
            "portable_debug": ["-DCROSSDESK_PORTABLE=1", "-DCROSSDESK_DEBUG"],
        }.items():
            binary = directory / name
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "apps/desktop/src"), *defines,
                            str(source), "-o", str(binary)], check=True)
            policy, codepoints = subprocess.check_output([str(binary)], text=True).split("\n", 1)
            xml = "".join(chr(int(value)) for value in codepoints.split())
            cls.variants[name] = (policy, ET.fromstring(xml))

    def text(self, path):
        return self.variants["installed"][1].findtext(path, namespaces=NAMESPACE)

    def test_privilege_policy_matches_build_type(self):
        self.assertEqual({name: result[0] for name, result in self.variants.items()},
                         {"installed": "1", "portable": "0", "debug": "0", "portable_debug": "0"})

    def test_login_targets_same_interactive_user_without_password(self):
        sid = "S-1-5-21-123-456-789-1001"
        self.assertEqual(self.text("t:Triggers/t:LogonTrigger/t:UserId"), sid)
        self.assertEqual(self.text("t:Principals/t:Principal/t:UserId"), sid)
        self.assertEqual(self.text("t:Principals/t:Principal/t:LogonType"), "InteractiveToken")
        self.assertEqual(self.text("t:Principals/t:Principal/t:RunLevel"), "HighestAvailable")
        self.assertEqual(self.text("t:Triggers/t:LogonTrigger/t:Enabled"), "true")

    def test_command_is_not_shell_interpreted_and_unicode_round_trips(self):
        self.assertEqual(self.text("t:Actions/t:Exec/t:Command"), r"C:\程序 & Tools\CrossDesk.exe")
        self.assertEqual(self.text("t:Actions/t:Exec/t:WorkingDirectory"), r"C:\程序 & Tools")
        self.assertIsNone(self.text("t:Actions/t:Exec/t:Arguments"))
        task = self.variants["installed"][1]
        self.assertEqual(task.find("t:Actions", NAMESPACE).get("Context"),
                         task.find("t:Principals/t:Principal", NAMESPACE).get("id"))

    def test_remote_client_can_run_indefinitely_on_battery(self):
        self.assertEqual(self.text("t:Triggers/t:LogonTrigger/t:ExecutionTimeLimit"), "PT0S")
        self.assertEqual(self.text("t:Settings/t:DisallowStartIfOnBatteries"), "false")
        self.assertEqual(self.text("t:Settings/t:StopIfGoingOnBatteries"), "false")
        self.assertEqual(self.text("t:Settings/t:ExecutionTimeLimit"), "PT0S")
        self.assertEqual(self.text("t:Settings/t:MultipleInstancesPolicy"), "IgnoreNew")


if __name__ == "__main__":
    unittest.main()
