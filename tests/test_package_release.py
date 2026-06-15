#!/usr/bin/env python3
"""Tests for release bundle packaging."""

from __future__ import annotations

import os
import pathlib
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "scripts" / "package-release.sh"


class PackageReleaseTest(unittest.TestCase):
    def test_package_release_uses_configurable_build_and_dist_dirs(self) -> None:
        with tempfile.TemporaryDirectory(prefix="esp32-kvm-package-") as tmp:
            root = pathlib.Path(tmp)
            build = root / "build"
            dist = root / "dist"
            (build / "bootloader").mkdir(parents=True)
            (build / "partition_table").mkdir()
            (build / "bootloader" / "bootloader.bin").write_bytes(b"boot")
            (build / "partition_table" / "partition-table.bin").write_bytes(b"part")
            (build / "ota_data_initial.bin").write_bytes(b"ota")
            (build / "esp32_kvm.bin").write_bytes(b"app")

            env = os.environ.copy()
            env["ESP32_KVM_BUILD_DIR"] = str(build)
            env["ESP32_KVM_DIST_DIR"] = str(dist)
            env["ESP32_KVM_RAW_BASE_URL"] = "https://raw.githubusercontent.com/acme/esp32-kvm"
            result = subprocess.run(
                [str(SCRIPT), "--no-build", "--version", "vtest"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            bundle = dist / "esp32-kvm-vtest"
            self.assertTrue((bundle / "esp32_kvm.bin").is_file())
            self.assertTrue((bundle / "setup-linux-serial-console.sh").is_file())
            install = (bundle / "INSTALL.md").read_text()
            self.assertIn("https://raw.githubusercontent.com/acme/esp32-kvm/vtest/tools/host/setup-linux-serial-console.sh", install)
            self.assertNotIn("YOUR_ORG", install)
            self.assertIn("0x20000", (bundle / "manifest.txt").read_text())
            self.assertIn("esp32_kvm.bin", (bundle / "SHA256SUMS").read_text())
            self.assertIn("INSTALL.md", (bundle / "SHA256SUMS").read_text())
            self.assertTrue((dist / "esp32-kvm-vtest.tar.gz").is_file())

    def test_package_release_rejects_path_like_version(self) -> None:
        with tempfile.TemporaryDirectory(prefix="esp32-kvm-package-") as tmp:
            root = pathlib.Path(tmp)
            build = root / "build"
            (build / "bootloader").mkdir(parents=True)
            (build / "partition_table").mkdir()
            env = os.environ.copy()
            env["ESP32_KVM_BUILD_DIR"] = str(build)
            result = subprocess.run(
                [str(SCRIPT), "--no-build", "--version", "../bad"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(result.returncode, 2)
            self.assertIn("invalid release version", result.stderr)


if __name__ == "__main__":
    unittest.main()
