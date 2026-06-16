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
            tty = root / "ttyTEST"
            tty.write_text("tty")
            env = os.environ.copy()
            env["SERIALITE_RELEASE_ASSET_URL"] = source.as_uri()
            env["SERIALITE_CACHE_DIR"] = str(cache)
            env["SERIALITE_TEST_MODE"] = "1"

            result = subprocess.run(
                [str(SCRIPT), "--port", str(tty), "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f"python3 -m esptool --chip esp32s3 -p {tty}", result.stdout)
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
            tty = root / "ttyTEST"
            tty.write_text("tty")
            env = os.environ.copy()
            env["SERIALITE_RELEASE_ASSET_URL"] = source.as_uri()
            env["SERIALITE_CACHE_DIR"] = str(cache)
            env["SERIALITE_TEST_MODE"] = "1"

            first = subprocess.run(
                [str(SCRIPT), "--port", str(tty), "--dry-run"],
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
                [str(SCRIPT), "--port", str(tty), "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertIn("Using cached firmware", second.stderr)

    def test_autodetects_single_esp32_serial_device(self) -> None:
        with tempfile.TemporaryDirectory(prefix="serialite-flash-") as tmp:
            root = pathlib.Path(tmp)
            source = self.make_bundle(root)
            cache = root / "cache"
            dev = root / "dev"
            by_id = dev / "serial" / "by-id"
            tty = dev / "ttyACM7"
            by_id.mkdir(parents=True)
            tty.write_text("tty")
            (by_id / "usb-Espressif_USB_JTAG_serial_debug_unit_TEST-if00").symlink_to("../../ttyACM7")
            env = os.environ.copy()
            env["SERIALITE_RELEASE_ASSET_URL"] = source.as_uri()
            env["SERIALITE_CACHE_DIR"] = str(cache)
            env["SERIALITE_DEV_DIR"] = str(dev)
            env["SERIALITE_TEST_MODE"] = "1"

            result = subprocess.run(
                [str(SCRIPT), "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f"-p {tty}", result.stdout)

    def test_autodetect_fails_when_multiple_devices_exist(self) -> None:
        with tempfile.TemporaryDirectory(prefix="serialite-flash-") as tmp:
            root = pathlib.Path(tmp)
            source = self.make_bundle(root)
            cache = root / "cache"
            dev = root / "dev"
            by_id = dev / "serial" / "by-id"
            by_id.mkdir(parents=True)
            for index in (0, 1):
                tty = dev / f"ttyACM{index}"
                tty.write_text("tty")
                (by_id / f"usb-Espressif_USB_JTAG_serial_debug_unit_TEST{index}-if00").symlink_to(f"../../ttyACM{index}")
            env = os.environ.copy()
            env["SERIALITE_RELEASE_ASSET_URL"] = source.as_uri()
            env["SERIALITE_CACHE_DIR"] = str(cache)
            env["SERIALITE_DEV_DIR"] = str(dev)
            env["SERIALITE_TEST_MODE"] = "1"

            result = subprocess.run(
                [str(SCRIPT), "--dry-run"],
                cwd=REPO_ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn("Multiple ESP32 serial devices found", result.stderr)


if __name__ == "__main__":
    unittest.main()
