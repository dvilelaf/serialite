#!/usr/bin/env python3
"""Validate production-update prerequisites that are easy to regress."""

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
FLASH_SIZE = 0x400000
MIN_OTA_SLOT_SIZE = 0x1E0000
PROD_DEFAULTS = REPO_ROOT / "sdkconfig.prod.defaults"
SDKCONFIG_DEFAULTS = "sdkconfig.defaults;sdkconfig.prod.defaults"
UNSAFE_KEY_NAMES = {
    "test",
    "tests",
    "example",
    "examples",
    "default",
    "defaults",
    "demo",
    "dummy",
    "sample",
    "samples",
    "secure_boot_signing_key",
    "secure_boot_signing_key.pem",
}
REQUIRED_PROD_SYMBOLS = {
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y",
    "CONFIG_SECURE_BOOT": "y",
    "CONFIG_SECURE_BOOT_V2_ENABLED": "y",
    "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES": "y",
    "CONFIG_SECURE_SIGNED_APPS": "y",
    "CONFIG_SECURE_SIGNED_ON_UPDATE": "y",
    "CONFIG_SECURE_FLASH_ENC_ENABLED": "y",
    "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE": "y",
    "CONFIG_NVS_ENCRYPTION": "y",
}


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


def verify_required_prod_symbols(assignments: dict[str, str]) -> None:
    for symbol, expected in REQUIRED_PROD_SYMBOLS.items():
        require(assignments.get(symbol) == expected, f"production profile missing effective {symbol}={expected}")


def verify_signing_key_path(key_path: str) -> None:
    require(key_path, "production profile must reference a non-empty signing key path")
    require(os.path.isabs(key_path), "production signing key path must be absolute and outside the repo")

    resolved_repo = REPO_ROOT.resolve()
    resolved_key = pathlib.Path(key_path)
    try:
        resolved_key.relative_to(resolved_repo)
        raise AssertionError("production signing key path must not be inside the repo")
    except ValueError:
        pass

    parts = [part.lower() for part in pathlib.PurePath(key_path).parts]
    basenames = {part for part in parts}
    basenames.add(pathlib.PurePath(key_path).stem.lower())
    unsafe_hits = sorted(UNSAFE_KEY_NAMES.intersection(basenames))
    if unsafe_hits:
        raise AssertionError(f"production signing key path uses unsafe sample/test name: {unsafe_hits[0]}")


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


def read_effective_prod_sdkconfig() -> str | None:
    if shutil.which("idf.py") is None:
        return None

    with tempfile.TemporaryDirectory(prefix="esp32-kvm-prod-config-") as temp_dir:
        sdkconfig_path = pathlib.Path(temp_dir) / "sdkconfig"
        build_dir = pathlib.Path(temp_dir) / "build"
        subprocess.run(
            [
                "idf.py",
                "-B",
                str(build_dir),
                "-D",
                f"SDKCONFIG={sdkconfig_path}",
                "-D",
                f"SDKCONFIG_DEFAULTS={SDKCONFIG_DEFAULTS}",
                "reconfigure",
            ],
            cwd=REPO_ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        return sdkconfig_path.read_text()


def verify_prod_sdkconfig() -> None:
    require(PROD_DEFAULTS.exists(), "sdkconfig.prod.defaults must exist")
    raw_assignments = parse_sdkconfig_assignments(PROD_DEFAULTS.read_text())
    verify_required_prod_symbols(raw_assignments)

    signing_key = raw_assignments.get("CONFIG_SECURE_BOOT_SIGNING_KEY", "")
    verify_signing_key_path(signing_key)

    effective_text = read_effective_prod_sdkconfig()
    if effective_text is not None:
        effective_assignments = parse_sdkconfig_assignments(effective_text)
        verify_required_prod_symbols(effective_assignments)
        require(
            effective_assignments.get("CONFIG_SECURE_BOOT_SIGNING_KEY") == signing_key,
            "effective production config must use the reviewed signing key path",
        )


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
