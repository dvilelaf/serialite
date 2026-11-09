#!/usr/bin/env python3
"""Tests for tools/flash-latest-firmware.sh against a fake local release."""

from __future__ import annotations

import hashlib
import io
import os
import pathlib
import subprocess
import tarfile
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "tools" / "flash-latest-firmware.sh"
VERSION = "vtest"
NAME = f"serialite-{VERSION}"
FLASH_ARGS = (
    "--flash_mode dio --flash_freq 80m --flash_size 16MB\n"
    "0x0 bootloader.bin\n0x20000 serialite.bin\n0x8000 partition-table.bin\n0xf000 ota_data_initial.bin\n"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class FlashLatestFirmwareTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="serialite-flash-")
        self.root = pathlib.Path(self.tmp.name)
        self.release = self.root / "releases"
        self.assets = self.release / "download" / VERSION
        self.assets.mkdir(parents=True)
        self.dev = self.root / "dev"
        (self.dev / "serial" / "by-id").mkdir(parents=True)
        self.env = {
            **os.environ,
            "SERIALITE_TEST_MODE": "1",
            "SERIALITE_RELEASE_BASE_URL": self.release.as_uri(),
            "SERIALITE_CACHE_DIR": str(self.root / "cache"),
            "SERIALITE_DEV_DIR": str(self.dev),
        }

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def publish(
        self,
        files: dict[str, bytes] | None = None,
        extra_member: str | None = None,
        profile: str = "lab",
    ) -> None:
        files = files or {
            "bootloader.bin": b"boot",
            "partition-table.bin": b"part",
            "ota_data_initial.bin": b"ota",
            "serialite.bin": b"app",
            "flash_args": FLASH_ARGS.encode(),
            "manifest.txt": f"Serialite {VERSION}\nProfile: {profile}\n".encode(),
        }
        files = dict(files)
        files["SHA256SUMS"] = "".join(
            f"{sha256(data)}  {name}\n" for name, data in sorted(files.items())
        ).encode()
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as archive:
            for name, data in files.items():
                info = tarfile.TarInfo(f"{NAME}/{name}")
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
            if extra_member is not None:
                info = tarfile.TarInfo(extra_member)
                info.size = 1
                archive.addfile(info, io.BytesIO(b"x"))
        tarball = buf.getvalue()
        (self.assets / f"{NAME}.tar.gz").write_bytes(tarball)
        (self.assets / f"{NAME}.tar.gz.sha256").write_text(
            f"{sha256(tarball)}  {NAME}.tar.gz\n"
        )

    def add_device(self, name: str) -> pathlib.Path:
        tty = self.dev / name
        tty.write_text("tty")
        (
            self.dev
            / "serial"
            / "by-id"
            / f"usb-Espressif_USB_JTAG_serial_debug_unit_{name}-if00"
        ).symlink_to(f"../../{name}")
        return tty

    def run_script(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(SCRIPT), "--version", VERSION, *args],
            cwd=REPO_ROOT,
            env=self.env,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_dry_run_verifies_and_flashes_with_the_bundle_flash_args(self) -> None:
        self.publish()
        tty = self.add_device("ttyACM7")
        result = self.run_script("--port", str(tty), "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"python3 -m esptool --chip esp32s3 -p {tty}", result.stdout)
        self.assertIn("write_flash @flash_args", result.stdout)
        self.assertIn("0x20000 serialite.bin", result.stdout)
        self.assertIn("0xf000 ota_data_initial.bin", result.stdout)

    def test_tampered_tarball_is_rejected_and_removed_from_cache(self) -> None:
        self.publish()
        tarball = self.assets / f"{NAME}.tar.gz"
        tarball.write_bytes(tarball.read_bytes() + b"tampered")
        result = self.run_script("--port", str(self.add_device("ttyACM1")), "--dry-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum mismatch", result.stderr)
        self.assertFalse((self.root / "cache" / f"{NAME}.tar.gz").exists())

    def test_missing_checksum_asset_is_fatal(self) -> None:
        self.publish()
        (self.assets / f"{NAME}.tar.gz.sha256").unlink()
        result = self.run_script("--port", str(self.add_device("ttyACM1")), "--dry-run")
        self.assertNotEqual(result.returncode, 0)

    def test_archive_paths_outside_the_bundle_are_rejected(self) -> None:
        self.publish(extra_member="../evil.sh")
        result = self.run_script("--port", str(self.add_device("ttyACM1")), "--dry-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unexpected path", result.stderr)
        self.assertFalse((self.root / "evil.sh").exists())

    def test_bundle_contents_must_match_sha256sums(self) -> None:
        self.publish()
        # Re-pack with a file changed after SHA256SUMS was computed.
        buf = io.BytesIO()
        with (
            tarfile.open(self.assets / f"{NAME}.tar.gz") as source,
            tarfile.open(fileobj=buf, mode="w:gz") as target,
        ):
            for member in source.getmembers():
                data = source.extractfile(member).read()
                if member.name.endswith("serialite.bin"):
                    data = b"evil"
                    member.size = len(data)
                target.addfile(member, io.BytesIO(data))
        (self.assets / f"{NAME}.tar.gz").write_bytes(buf.getvalue())
        (self.assets / f"{NAME}.tar.gz.sha256").write_text(
            f"{sha256(buf.getvalue())}  {NAME}.tar.gz\n"
        )
        result = self.run_script("--port", str(self.add_device("ttyACM1")), "--dry-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA256SUMS", result.stderr)

    def test_production_bundles_are_refused(self) -> None:
        self.publish(profile="prod")
        result = self.run_script("--port", str(self.add_device("ttyACM1")), "--dry-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("production", result.stderr)

    def test_uses_cached_release_when_offline(self) -> None:
        self.publish()
        tty = self.add_device("ttyACM1")
        self.assertEqual(self.run_script("--port", str(tty), "--dry-run").returncode, 0)
        for asset in self.assets.iterdir():
            asset.unlink()
        second = self.run_script("--port", str(tty), "--dry-run")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("Using cached firmware", second.stderr)

    def test_autodetects_a_single_device(self) -> None:
        self.publish()
        tty = self.add_device("ttyACM7")
        result = self.run_script("--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"-p {tty}", result.stdout)

    def test_autodetect_refuses_to_guess_between_devices(self) -> None:
        self.publish()
        self.add_device("ttyACM0")
        self.add_device("ttyACM1")
        result = self.run_script("--dry-run")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Multiple ESP32 serial devices found", result.stderr)


if __name__ == "__main__":
    unittest.main()
