#!/usr/bin/env python3
"""Tests for the end-user firmware flashing helper."""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import tarfile
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "tools" / "flash-latest-firmware.sh"


class FlashLatestFirmwareTest(unittest.TestCase):
    def make_bundle(self, root: pathlib.Path) -> pathlib.Path:
        bundle = root / "serialite-vtest"
        bundle.mkdir()
        for name, data in {
            "bootloader.bin": b"boot",
            "partition-table.bin": b"part",
            "ota_data_initial.bin": b"ota",
            "serialite.bin": b"app",
        }.items():
            (bundle / name).write_bytes(data)
        with (bundle / "SHA256SUMS").open("w") as checksums:
            subprocess.run(
                ["sha256sum", "bootloader.bin", "partition-table.bin", "ota_data_initial.bin", "serialite.bin"],
                cwd=bundle,
                text=True,
                stdout=checksums,
                check=True,
            )
        tarball = root / "serialite-vtest.tar.gz"
        with tarfile.open(tarball, "w:gz") as archive:
            archive.add(bundle, arcname=bundle.name)
        return tarball

    def test_dry_run_downloads_extracts_verifies_and_prints_flash_command(self) -> None:
        with tempfile.TemporaryDirectory(prefix="serialite-flash-") as tmp:
            root = pathlib.Path(tmp)
            source = self.make_bundle(root)
            cache = root / "cache"
            env = os.environ.copy()
            env["SERIALITE_RELEASE_ASSET_URL"] = source.as_uri()
            env["SERIALITE_CACHE_DIR"] = str(cache)

            result = subprocess.run(
                [str(SCRIPT), "--port", "/dev/ttyTEST", "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("python3 -m esptool --chip esp32s3 -p /dev/ttyTEST", result.stdout)
            self.assertIn("0x0 bootloader.bin", result.stdout)
            self.assertIn("0x8000 partition-table.bin", result.stdout)
            self.assertIn("0xf000 ota_data_initial.bin", result.stdout)
            self.assertIn("0x20000 serialite.bin", result.stdout)
            self.assertTrue((cache / "serialite-vtest" / "serialite.bin").is_file())

    def test_uses_cached_tarball_when_source_disappears(self) -> None:
        with tempfile.TemporaryDirectory(prefix="serialite-flash-") as tmp:
            root = pathlib.Path(tmp)
            source = self.make_bundle(root)
            cache = root / "cache"
            env = os.environ.copy()
            env["SERIALITE_RELEASE_ASSET_URL"] = source.as_uri()
            env["SERIALITE_CACHE_DIR"] = str(cache)

            first = subprocess.run(
                [str(SCRIPT), "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            source.unlink()
            shutil.rmtree(cache / "serialite-vtest")

            second = subprocess.run(
                [str(SCRIPT), "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertIn("Using cached firmware", second.stderr)


if __name__ == "__main__":
    unittest.main()
