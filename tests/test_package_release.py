#!/usr/bin/env python3
"""Tests for scripts/package-release.sh with a fake build directory."""

from __future__ import annotations

import hashlib
import os
import pathlib
import subprocess
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "scripts" / "package-release.sh"
FLASH_ARGS = (
    "--flash_mode dio --flash_freq 80m --flash_size 16MB\n"
    "0x0 bootloader/bootloader.bin\n0x20000 serialite.bin\n"
    "0x8000 partition_table/partition-table.bin\n0xf000 ota_data_initial.bin\n"
)


class PackageReleaseTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="serialite-package-")
        root = pathlib.Path(self.tmp.name)
        self.build = root / "build"
        self.dist = root / "dist"
        (self.build / "bootloader").mkdir(parents=True)
        (self.build / "partition_table").mkdir()
        (self.build / "bootloader" / "bootloader.bin").write_bytes(b"boot")
        (self.build / "partition_table" / "partition-table.bin").write_bytes(b"part")
        (self.build / "ota_data_initial.bin").write_bytes(b"ota")
        (self.build / "serialite.bin").write_bytes(b"app")
        (self.build / "flash_args").write_text(FLASH_ARGS)
        self.env = {
            **os.environ,
            "SERIALITE_BUILD_DIR": str(self.build),
            "SERIALITE_DIST_DIR": str(self.dist),
        }

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def package(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(SCRIPT), "--no-build", "--allow-dirty", *args],
            cwd=REPO_ROOT,
            env=self.env,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_bundle_has_flattened_flash_args_manifest_and_checksums(self) -> None:
        result = self.package("--version", "vtest")
        self.assertEqual(result.returncode, 0, result.stderr)

        bundle = self.dist / "serialite-vtest"
        flash_args = (bundle / "flash_args").read_text()
        self.assertIn("0x0 bootloader.bin", flash_args)
        self.assertIn("0x8000 partition-table.bin", flash_args)
        self.assertIn("0xf000 ota_data_initial.bin", flash_args)
        manifest = (bundle / "manifest.txt").read_text()
        self.assertIn("Profile: lab", manifest)
        self.assertIn("0xf000 ota_data_initial.bin", manifest)
        self.assertNotIn("0xd000", manifest)
        for name in (
            "serialite.bin",
            "flash_args",
            "LICENSE",
            "THIRD_PARTY_NOTICES.md",
            "INSTALL.md",
        ):
            self.assertTrue((bundle / name).is_file(), name)
        sums = (bundle / "SHA256SUMS").read_text()
        self.assertIn("serialite.bin", sums)
        self.assertNotIn("SHA256SUMS", sums)

    def test_release_assets_include_a_separate_tarball_checksum(self) -> None:
        self.assertEqual(self.package("--version", "vtest").returncode, 0)
        tarball = self.dist / "serialite-vtest.tar.gz"
        expected = hashlib.sha256(tarball.read_bytes()).hexdigest()
        self.assertEqual(
            (self.dist / "serialite-vtest.tar.gz.sha256").read_text(),
            f"{expected}  serialite-vtest.tar.gz\n",
        )
        self.assertTrue((self.dist / "flash-latest-firmware.sh").is_file())
        self.assertTrue((self.dist / "setup-linux-serial-console.sh").is_file())

    def test_archive_is_reproducible(self) -> None:
        self.assertEqual(self.package("--version", "vtest").returncode, 0)
        first = (self.dist / "serialite-vtest.tar.gz").read_bytes()
        self.assertEqual(self.package("--version", "vtest").returncode, 0)
        self.assertEqual((self.dist / "serialite-vtest.tar.gz").read_bytes(), first)

    def test_rejects_path_like_version_and_unknown_profile(self) -> None:
        result = self.package("--version", "../bad")
        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid release version", result.stderr)
        self.assertEqual(
            self.package("--version", "vtest", "--profile", "debug").returncode, 2
        )


if __name__ == "__main__":
    unittest.main()
