#!/usr/bin/env python3
"""Unit tests for production profile verification policy."""

from __future__ import annotations

import pathlib
import sys
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import lint_production_profile as profile


class ProductionProfilePolicyTest(unittest.TestCase):
    def test_parse_active_sdkconfig_assignments_ignores_comments(self) -> None:
        assignments = profile.parse_sdkconfig_assignments(
            """
            # CONFIG_SECURE_BOOT is not set
            CONFIG_SECURE_BOOT=y
            # CONFIG_SECURE_BOOT_SIGNING_KEY="/tmp/test.pem"
            CONFIG_SECURE_BOOT_SIGNING_KEY="/run/secrets/serialite/signing.pem"
            """
        )

        self.assertEqual(assignments["CONFIG_SECURE_BOOT"], "y")
        self.assertEqual(assignments["CONFIG_SECURE_BOOT_SIGNING_KEY"], "/run/secrets/serialite/signing.pem")

    def test_required_symbols_include_secure_boot_and_flash_encryption_parents(self) -> None:
        assignments = {
            "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y",
            "CONFIG_SECURE_SIGNED_APPS": "y",
            "CONFIG_SECURE_SIGNED_ON_UPDATE": "y",
            "CONFIG_SECURE_BOOT_V2_ENABLED": "y",
            "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES": "y",
            "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE": "y",
            "CONFIG_NVS_ENCRYPTION": "y",
            "CONFIG_SECURE_BOOT_SIGNING_KEY": "/run/secrets/serialite/signing.pem",
        }

        with self.assertRaisesRegex(AssertionError, "CONFIG_SECURE_BOOT=y"):
            profile.verify_required_prod_symbols(assignments)

        assignments["CONFIG_SECURE_BOOT"] = "y"
        with self.assertRaisesRegex(AssertionError, "CONFIG_SECURE_FLASH_ENC_ENABLED=y"):
            profile.verify_required_prod_symbols(assignments)

    def test_signing_key_must_be_absolute_and_not_sample_named(self) -> None:
        for key_path in (
            "keys/serialite-secure-boot-signing-key.pem",
            "/run/secrets/serialite/test.pem",
            "/run/secrets/serialite/tests/key.pem",
            "/run/secrets/serialite/example.pem",
            "/run/secrets/serialite/default.pem",
            "/run/secrets/serialite/defaults.pem",
            "/run/secrets/serialite/demo.pem",
            "/run/secrets/serialite/dummy.pem",
            "/run/secrets/serialite/sample.pem",
            "/run/secrets/serialite/secure_boot_signing_key.pem",
        ):
            with self.subTest(key_path=key_path):
                with self.assertRaises(AssertionError):
                    profile.verify_signing_key_path(key_path)

        profile.verify_signing_key_path("/run/secrets/serialite/prod-kvm-2026.pem")

    def test_flash_size_comes_from_sdkconfig_defaults(self) -> None:
        assignments = profile.parse_sdkconfig_assignments(
            """
            CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
            CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
            """
        )

        self.assertEqual(profile.flash_size_from_assignments(assignments), 16 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
