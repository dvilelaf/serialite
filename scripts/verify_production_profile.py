#!/usr/bin/env python3
"""Validate production-update prerequisites that are easy to regress."""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
FLASH_SIZE = 0x400000
MIN_OTA_SLOT_SIZE = 0x1E0000


def parse_size(value: str) -> int:
    value = value.strip()
    match = re.fullmatch(r"([0-9]+)([KkMm]?)", value)
    if match is None:
        return int(value, 0)
    number = int(match.group(1))
    suffix = match.group(2).lower()
    if suffix == "k":
        return number * 1024
    if suffix == "m":
        return number * 1024 * 1024
    return number


def parse_partitions(path: pathlib.Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    with path.open(newline="") as handle:
        reader = csv.reader(line for line in handle if line.strip() and not line.lstrip().startswith("#"))
        for row in reader:
            if len(row) < 5:
                raise AssertionError(f"invalid partition row: {row}")
            rows.append(
                {
                    "name": row[0].strip(),
                    "type": row[1].strip(),
                    "subtype": row[2].strip(),
                    "offset": row[3].strip(),
                    "size": row[4].strip(),
                }
            )
    return rows


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def verify_partition_table() -> None:
    rows = parse_partitions(REPO_ROOT / "partitions.csv")
    names = {row["name"] for row in rows}
    require("otadata" in names, "partition table must include otadata for OTA rollback")

    ota_slots = [row for row in rows if row["type"] == "app" and row["subtype"].startswith("ota_")]
    require(len(ota_slots) >= 2, "partition table must include at least two OTA app slots")

    for expected in ("ota_0", "ota_1"):
        slot = next((row for row in ota_slots if row["subtype"] == expected), None)
        require(slot is not None, f"partition table must include {expected}")
        require(parse_size(slot["size"]) >= MIN_OTA_SLOT_SIZE, f"{expected} must be at least {MIN_OTA_SLOT_SIZE:#x} bytes")

    used = []
    for row in rows:
        offset = int(row["offset"], 0)
        size = parse_size(row["size"])
        used.append((offset, offset + size, row["name"]))
    used.sort()
    for idx, (start, end, name) in enumerate(used):
        require(end <= FLASH_SIZE, f"{name} exceeds 4MB flash")
        if idx > 0:
            prev_end = used[idx - 1][1]
            require(start >= prev_end, f"{name} overlaps previous partition")


def verify_prod_sdkconfig() -> None:
    path = REPO_ROOT / "sdkconfig.prod.defaults"
    require(path.exists(), "sdkconfig.prod.defaults must exist")
    text = path.read_text()
    required = [
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
        "CONFIG_SECURE_SIGNED_APPS=y",
        "CONFIG_SECURE_SIGNED_ON_UPDATE=y",
        "CONFIG_SECURE_BOOT_V2_ENABLED=y",
        "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y",
        "CONFIG_NVS_ENCRYPTION=y",
    ]
    for line in required:
        require(line in text, f"production profile missing {line}")
    require("CONFIG_SECURE_BOOT_SIGNING_KEY=" in text, "production profile must reference an external signing key path")
    require("test/" not in text and "test_" not in text, "production profile must not reference test signing keys")


def main() -> int:
    try:
        verify_partition_table()
        verify_prod_sdkconfig()
    except AssertionError as exc:
        print(f"production profile verification failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
