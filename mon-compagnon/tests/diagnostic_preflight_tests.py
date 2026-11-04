"""Offline regressions: no real esptool, serial open, flash or build."""
import hashlib
import importlib.util
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "preflight", ROOT / "scripts/check-diagnostic-preflight.py")
preflight = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preflight)


def table(app_offset=0x10000):
    entries = [(1, 2, 0x9000, 0x5000, b"nvs"),
               (1, 0, 0xe000, 0x2000, b"otadata"),
               (0, 16, app_offset, 0x140000, b"app0"),
               (0, 17, 0x150000, 0x140000, b"app1"),
               (1, 130, 0x290000, 0x160000, b"spiffs"),
               (1, 3, 0x3f0000, 0x10000, b"coredump")]
    raw = b"".join(struct.pack("<HBBII16sI", 0x50aa, *row, 0)
                   for row in entries)
    raw += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()
    return raw.ljust(3072, b"\xff")


class PreflightTests(unittest.TestCase):
    def test_security_requires_two_unambiguous_disabled_states(self):
        valid = "Secure Boot: Disabled\nFlash Encryption: Disabled\n"
        preflight.check_report("security", valid)
        for bad in ("", valid.replace("Disabled", "Enabled", 1),
                    "Secure Boot: Disabled", valid + "Secure Boot: Enabled\n",
                    valid + "Flash Encryption: Disabled\n"):
            with self.assertRaises(ValueError):
                preflight.check_report("security", bad)

    def test_real_esptool_spacing_and_repeated_mac(self):
        mac = "MAC:                44:b1:76:b6:a5:74\n"
        preflight.check_report("target", mac + mac)
        for bad in ("", mac.replace("a5:74", "a5:75"),
                    mac + "MAC: 00:00:00:00:00:00\n"):
            with self.assertRaises(ValueError):
                preflight.check_report("target", bad)

    def test_changed_partition_rejected_even_with_valid_md5(self):
        preflight.check_partitions(table())
        for bad in (table(0x20000), table()[:-1], b"\x00" * 3072,
                    table()[:208] + b"\x00" * 16 + table()[224:]):
            with self.assertRaises(ValueError):
                preflight.check_partitions(bad)

    def test_launcher_default_update_and_security_abort(self):
        # Stage the real launcher with ONLY its device enumeration replaced.
        # HOME holds a fake esptool; no actual /dev node is ever opened.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scripts = root / "scripts"
            scripts.mkdir()
            launcher = (ROOT / "scripts/Preparer_module.command").read_text()
            self.assertIn("ports=(/dev/cu.usbmodem*)", launcher)
            (scripts / "Preparer_module.command").write_text(launcher.replace(
                "ports=(/dev/cu.usbmodem*)", "ports=(/dev/cu.usbmodem14501)"))
            shutil.copy(ROOT / "scripts/check-diagnostic-preflight.py", scripts)
            (scripts / "build-diagnostic.sh").write_text("#!/bin/sh\nexit 0\n")
            images = root / "artifacts/diagnostic"
            images.mkdir(parents=True)
            (images / "diagnostic.ino.bin").write_bytes(b"test-only")
            (images / "diagnostic.ino.partitions.bin").write_bytes(table())
            core = root / "Library/Arduino15/packages/esp32/hardware/esp32/3.3.8/tools/partitions"
            core.mkdir(parents=True)
            (core / "boot_app0.bin").touch()
            tool = root / "Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool"
            tool.parent.mkdir(parents=True)
            tool.write_text("""#!/bin/sh
echo "$*" >> "$TEST_CALL_LOG"
case " $* " in
  *' read-mac '*) echo 'MAC:                44:b1:76:b6:a5:74';;
  *' flash-id '*) echo 'Detected flash size: 16MB';;
  *' get-security-info '*) echo "Secure Boot: $TEST_BOOT"; echo 'Flash Encryption: Disabled';;
esac
""")
            tool.chmod(0o700)
            log = root / "calls"
            env = dict(os.environ, HOME=str(root), TEST_CALL_LOG=str(log))
            for args, boot, success, erase in [([], "Disabled", True, False),
                                               (["--update-only"], "Disabled", True, False),
                                               ([], "Enabled", False, False),
                                               (["--erase-and-install"], "Disabled", True, True)]:
                log.write_text("")
                result = subprocess.run(["bash", str(scripts / "Preparer_module.command"), *args],
                                        env=dict(env, TEST_BOOT=boot), stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                calls = log.read_text()
                self.assertEqual(result.returncode == 0, success, result.stderr.decode())
                self.assertEqual("write-flash" in calls, success)
                self.assertEqual("erase-flash" in calls, erase)


if __name__ == "__main__":
    unittest.main()
