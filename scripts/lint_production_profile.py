#!/usr/bin/env python3
"""Static checks for the production profile and the partition tables.

A broken production profile bricks boards: the first boot burns eFuses and
the image can never be replaced over USB again. These checks catch the
mistakes that only show up on real hardware.

When idf.py is on PATH the effective configuration is also resolved with
`idf.py reconfigure`, so Kconfig dependencies cannot silently drop a symbol.
"""

from __future__ import annotations

import csv
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
LAB_DEFAULTS = REPO_ROOT / "sdkconfig.defaults"
PROD_DEFAULTS = REPO_ROOT / "sdkconfig.prod.defaults"
LAB_PARTITIONS = REPO_ROOT / "partitions.csv"
OTA_POLICY_HEADER = REPO_ROOT / "components" / "ota_update" / "include" / "ota_update_policy.h"
SDKCONFIG_DEFAULTS = "sdkconfig.defaults;sdkconfig.prod.defaults"
DEFAULT_FLASH_SIZE = 0x400000
DEFAULT_PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0x1000
# The Secure Boot v2 + release flash encryption bootloader is about 0x9000 bytes.
MIN_PROD_PARTITION_TABLE_OFFSET = 0xA000

UNSAFE_KEY_NAMES = {
    "test", "tests", "example", "examples", "default", "defaults", "demo", "dummy", "sample", "samples",
    "secure_boot_signing_key", "secure_boot_signing_key.pem",
}
REQUIRED_PROD_SYMBOLS = {
    "CONFIG_SECURE_BOOT": "y",
    "CONFIG_SECURE_BOOT_V2_ENABLED": "y",
    "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES": "y",
    "CONFIG_SECURE_SIGNED_APPS": "y",
    "CONFIG_SECURE_SIGNED_ON_UPDATE": "y",
    "CONFIG_SECURE_FLASH_ENC_ENABLED": "y",
    "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE": "y",
    "CONFIG_NVS_ENCRYPTION": "y",
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y",
    "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK": "y",
    "CONFIG_SERIALITE_HTTPS": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM": "y",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def parse_size(value: str) -> int:
    value = value.strip()
    match = re.fullmatch(r"([0-9]+)([KkMm](?:[Bb])?)?", value)
    if match is None:
        return int(value, 0)
    number = int(match.group(1))
    suffix = (match.group(2) or "").lower()
    if suffix in ("k", "kb"):
        return number * 1024
    if suffix in ("m", "mb"):
        return number * 1024 * 1024
    return number


def parse_sdkconfig_assignments(text: str) -> dict[str, str]:
    assignments: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
            value = value[1:-1]
        assignments[name.strip()] = value
    return assignments


def flash_size_from_assignments(assignments: dict[str, str]) -> int:
    value = assignments.get("CONFIG_ESPTOOLPY_FLASHSIZE")
    return parse_size(value) if value else DEFAULT_FLASH_SIZE


def parse_partitions(path: pathlib.Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    with path.open(newline="") as handle:
        reader = csv.reader(line for line in handle if line.strip() and not line.lstrip().startswith("#"))
        for row in reader:
            require(len(row) >= 5, f"{path.name}: invalid partition row: {row}")
            rows.append({
                "name": row[0].strip(),
                "type": row[1].strip(),
                "subtype": row[2].strip(),
                "offset": row[3].strip(),
                "size": row[4].strip(),
                "flags": row[5].strip() if len(row) > 5 else "",
            })
    return rows


def ota_policy_max_bytes() -> int:
    match = re.search(r"#define OTA_UPDATE_MAX_IMAGE_BYTES (0x[0-9A-Fa-f]+)U", OTA_POLICY_HEADER.read_text())
    require(match is not None, "OTA_UPDATE_MAX_IMAGE_BYTES not found in ota_update_policy.h")
    return int(match.group(1), 16)


def verify_partition_table(path: pathlib.Path, flash_size: int, table_offset: int, need_nvs_keys: bool) -> None:
    rows = parse_partitions(path)
    names = {row["name"] for row in rows}
    require("otadata" in names, f"{path.name}: needs otadata for OTA rollback")

    ota_slots = [row for row in rows if row["type"] == "app" and row["subtype"] in ("ota_0", "ota_1")]
    require(len(ota_slots) == 2, f"{path.name}: needs ota_0 and ota_1")
    max_image = ota_policy_max_bytes()
    for slot in ota_slots:
        require(parse_size(slot["size"]) == max_image,
                f"{path.name}: {slot['name']} must be {max_image:#x} bytes to match OTA_UPDATE_MAX_IMAGE_BYTES")

    keys = [row for row in rows if row["subtype"] == "nvs_keys"]
    if need_nvs_keys:
        require(len(keys) == 1, f"{path.name}: NVS encryption with flash encryption needs one nvs_keys partition")
        require("encrypted" in keys[0]["flags"], f"{path.name}: nvs_keys must be flagged encrypted")

    spans = sorted((int(r["offset"], 0), int(r["offset"], 0) + parse_size(r["size"]), r["name"]) for r in rows)
    require(spans[0][0] >= table_offset + PARTITION_TABLE_SIZE,
            f"{path.name}: {spans[0][2]} overlaps the partition table at {table_offset:#x}")
    for index, (start, end, name) in enumerate(spans):
        require(end <= flash_size, f"{path.name}: {name} exceeds the {flash_size:#x}-byte flash")
        if index > 0:
            require(start >= spans[index - 1][1], f"{path.name}: {name} overlaps {spans[index - 1][2]}")


def verify_required_prod_symbols(assignments: dict[str, str]) -> None:
    for symbol, expected in REQUIRED_PROD_SYMBOLS.items():
        require(assignments.get(symbol) == expected, f"production profile missing effective {symbol}={expected}")
    require(
        assignments.get("CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC") == "y"
        or assignments.get("CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC") == "y",
        "production profile must pick an NVS key protection scheme",
    )
    require(int(assignments.get("CONFIG_BOOTLOADER_APP_SECURE_VERSION", "0"), 0) >= 1,
            "production profile needs CONFIG_BOOTLOADER_APP_SECURE_VERSION >= 1")
    offset = int(assignments.get("CONFIG_PARTITION_TABLE_OFFSET", hex(DEFAULT_PARTITION_TABLE_OFFSET)), 0)
    require(offset >= MIN_PROD_PARTITION_TABLE_OFFSET,
            f"production partition table offset {offset:#x} leaves no room for the signed bootloader")


def verify_signing_key_path(key_path: str) -> None:
    require(bool(key_path), "production profile must reference a signing key path")
    require(os.path.isabs(key_path), "production signing key path must be absolute and outside the repo")
    try:
        pathlib.Path(key_path).resolve().relative_to(REPO_ROOT.resolve())
        raise AssertionError("production signing key path must not be inside the repo")
    except ValueError:
        pass
    parts = {part.lower() for part in pathlib.PurePath(key_path).parts}
    parts.add(pathlib.PurePath(key_path).stem.lower())
    unsafe = sorted(UNSAFE_KEY_NAMES.intersection(parts))
    require(not unsafe, f"production signing key path uses a sample/test name: {unsafe[0] if unsafe else ''}")


def read_effective_prod_sdkconfig() -> str | None:
    if shutil.which("idf.py") is None:
        return None
    with tempfile.TemporaryDirectory(prefix="serialite-prod-config-") as temp_dir:
        sdkconfig_path = pathlib.Path(temp_dir) / "sdkconfig"
        subprocess.run(
            ["idf.py", "-B", str(pathlib.Path(temp_dir) / "build"), "-D", f"SDKCONFIG={sdkconfig_path}",
             "-D", f"SDKCONFIG_DEFAULTS={SDKCONFIG_DEFAULTS}", "reconfigure"],
            cwd=REPO_ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
        )
        return sdkconfig_path.read_text()


def main() -> int:
    try:
        lab = parse_sdkconfig_assignments(LAB_DEFAULTS.read_text())
        prod = {**lab, **parse_sdkconfig_assignments(PROD_DEFAULTS.read_text())}
        flash_size = flash_size_from_assignments(lab)

        verify_partition_table(LAB_PARTITIONS, flash_size, DEFAULT_PARTITION_TABLE_OFFSET, need_nvs_keys=False)
        verify_required_prod_symbols(prod)
        verify_signing_key_path(prod.get("CONFIG_SECURE_BOOT_SIGNING_KEY", ""))
        prod_partitions = REPO_ROOT / prod.get("CONFIG_PARTITION_TABLE_CUSTOM_FILENAME", "")
        require(prod_partitions.is_file(), f"production partition table {prod_partitions.name} not found")
        verify_partition_table(
            prod_partitions,
            flash_size,
            int(prod["CONFIG_PARTITION_TABLE_OFFSET"], 0),
            need_nvs_keys=prod.get("CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC") == "y",
        )

        effective = read_effective_prod_sdkconfig()
        if effective is not None:
            resolved = parse_sdkconfig_assignments(effective)
            verify_required_prod_symbols(resolved)
            require(resolved.get("CONFIG_SECURE_BOOT_SIGNING_KEY") == prod.get("CONFIG_SECURE_BOOT_SIGNING_KEY"),
                    "effective production config must use the reviewed signing key path")
    except AssertionError as exc:
        print(f"production profile check failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
