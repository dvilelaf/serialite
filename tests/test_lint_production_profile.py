#!/usr/bin/env python3
"""Unit tests for scripts/lint_production_profile.py."""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import lint_production_profile as profile

GOOD_PROD = {
    **profile.REQUIRED_PROD_SYMBOLS,
    "CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC": "y",
    "CONFIG_BOOTLOADER_APP_SECURE_VERSION": "1",
    "CONFIG_PARTITION_TABLE_OFFSET": "0xC000",
}


class ProductionProfileTest(unittest.TestCase):
    def test_parse_active_sdkconfig_assignments_ignores_comments(self) -> None:
        assignments = profile.parse_sdkconfig_assignments(
            """
            # CONFIG_SECURE_BOOT is not set
            CONFIG_SECURE_BOOT=y
            CONFIG_SECURE_BOOT_SIGNING_KEY="/run/secrets/serialite/signing.pem"
            """
        )
        self.assertEqual(assignments["CONFIG_SECURE_BOOT"], "y")
        self.assertEqual(
            assignments["CONFIG_SECURE_BOOT_SIGNING_KEY"],
            "/run/secrets/serialite/signing.pem",
        )

    def test_complete_profile_passes(self) -> None:
        profile.verify_required_prod_symbols(GOOD_PROD)

    def test_each_required_symbol_is_enforced(self) -> None:
        for symbol in profile.REQUIRED_PROD_SYMBOLS:
            with self.subTest(symbol=symbol):
                broken = {k: v for k, v in GOOD_PROD.items() if k != symbol}
                with self.assertRaisesRegex(AssertionError, symbol):
                    profile.verify_required_prod_symbols(broken)

    def test_nvs_key_scheme_secure_version_and_table_offset_are_enforced(self) -> None:
        broken = {
            k: v
            for k, v in GOOD_PROD.items()
            if k != "CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC"
        }
        with self.assertRaisesRegex(AssertionError, "key protection"):
            profile.verify_required_prod_symbols(broken)
        with self.assertRaisesRegex(AssertionError, "SECURE_VERSION"):
            profile.verify_required_prod_symbols(
                {**GOOD_PROD, "CONFIG_BOOTLOADER_APP_SECURE_VERSION": "0"}
            )
        with self.assertRaisesRegex(AssertionError, "signed bootloader"):
            profile.verify_required_prod_symbols(
                {**GOOD_PROD, "CONFIG_PARTITION_TABLE_OFFSET": "0x8000"}
            )

    def test_signing_key_must_be_absolute_and_not_sample_named(self) -> None:
        for key_path in (
            "keys/serialite-secure-boot-signing-key.pem",
            "/run/secrets/serialite/test.pem",
            "/run/secrets/serialite/tests/key.pem",
            "/run/secrets/serialite/example.pem",
            "/run/secrets/serialite/demo.pem",
            "/run/secrets/serialite/secure_boot_signing_key.pem",
            str(REPO_ROOT / "prod-signing-key.pem"),
        ):
            with self.subTest(key_path=key_path), self.assertRaises(AssertionError):
                profile.verify_signing_key_path(key_path)
        profile.verify_signing_key_path("/run/secrets/serialite/prod-signing-key.pem")

    def _table(self, text: str) -> pathlib.Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "partitions.csv"
        path.write_text(text)
        return path

    def test_partition_table_rules(self) -> None:
        slot = hex(profile.ota_policy_max_bytes())
        good = self._table(
            "nvs,data,nvs,0xd000,0x6000,\n"
            "otadata,data,ota,0x13000,0x2000,\n"
            "nvs_keys,data,nvs_keys,0x16000,0x1000,encrypted\n"
            f"ota_0,app,ota_0,0x20000,{slot},\n"
            f"ota_1,app,ota_1,0x620000,{slot},\n"
        )
        profile.verify_partition_table(
            good, 16 * 1024 * 1024, 0xC000, need_nvs_keys=True
        )
        with self.assertRaisesRegex(AssertionError, "overlaps the partition table"):
            profile.verify_partition_table(
                good, 16 * 1024 * 1024, 0xD000, need_nvs_keys=True
            )
        with self.assertRaisesRegex(AssertionError, "exceeds"):
            profile.verify_partition_table(
                good, 4 * 1024 * 1024, 0xC000, need_nvs_keys=True
            )

        no_keys = self._table(
            "nvs,data,nvs,0xd000,0x6000,\n"
            "otadata,data,ota,0x13000,0x2000,\n"
            f"ota_0,app,ota_0,0x20000,{slot},\n"
            f"ota_1,app,ota_1,0x620000,{slot},\n"
        )
        with self.assertRaisesRegex(AssertionError, "nvs_keys"):
            profile.verify_partition_table(
                no_keys, 16 * 1024 * 1024, 0xC000, need_nvs_keys=True
            )

        small_slot = self._table(
            "otadata,data,ota,0xf000,0x2000,\n"
            "ota_0,app,ota_0,0x20000,0x100000,\n"
            "ota_1,app,ota_1,0x120000,0x100000,\n"
        )
        with self.assertRaisesRegex(AssertionError, "OTA_UPDATE_MAX_IMAGE_BYTES"):
            profile.verify_partition_table(
                small_slot, 16 * 1024 * 1024, 0x8000, need_nvs_keys=False
            )

    def test_repository_profile_passes(self) -> None:
        self.assertEqual(profile.main(), 0)


if __name__ == "__main__":
    unittest.main()
