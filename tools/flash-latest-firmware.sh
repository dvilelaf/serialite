#!/usr/bin/env bash
set -euo pipefail

repo="dvilelaf/serialite"
version="latest"
port="${PORT:-${SERIALITE_FLASH_PORT:-/dev/ttyACM0}}"
baud="${SERIALITE_FLASH_BAUD:-460800}"
python_bin="${SERIALITE_PYTHON:-python3}"
force=0
dry_run=0
cache_dir="${SERIALITE_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/serialite/firmware}"
asset_url="${SERIALITE_RELEASE_ASSET_URL:-}"

usage() {
    cat <<'EOF'
Usage:
  tools/flash-latest-firmware.sh [--port /dev/ttyACM0] [--version vX.Y.Z] [--force] [--dry-run]

Downloads the Serialite firmware bundle from GitHub Releases if it is not cached,
verifies SHA256SUMS, and flashes the ESP32-S3 with esptool.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port|-p)
            if [ "$#" -lt 2 ]; then
                echo "missing value for --port" >&2
                exit 2
            fi
            port="$2"
            shift 2
            ;;
        --version)
            if [ "$#" -lt 2 ]; then
                echo "missing value for --version" >&2
                exit 2
            fi
            version="$2"
            shift 2
            ;;
        --force)
            force=1
            shift
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "$1 not found" >&2
        exit 127
    fi
}

resolve_latest_asset_url() {
    python3 - "$repo" <<'PY'
import json
import sys
import urllib.request

repo = sys.argv[1]
with urllib.request.urlopen(f"https://api.github.com/repos/{repo}/releases/latest", timeout=20) as response:
    release = json.load(response)
for asset in release.get("assets", []):
    name = asset.get("name", "")
    if name.startswith("serialite-") and name.endswith(".tar.gz"):
        print(asset["browser_download_url"])
        sys.exit(0)
raise SystemExit("no serialite firmware tarball found in latest release")
PY
}

if [ -z "$asset_url" ]; then
    if [ "$version" = "latest" ]; then
        require_command python3
        asset_url="$(resolve_latest_asset_url)"
    else
        asset_url="https://github.com/${repo}/releases/download/${version}/serialite-${version}.tar.gz"
    fi
fi

tarball_name="$(basename "${asset_url%%\?*}")"
if [ -z "$tarball_name" ] || [ "$tarball_name" = "." ]; then
    echo "unable to infer firmware tarball name from: $asset_url" >&2
    exit 2
fi

mkdir -p "$cache_dir"
tarball="$cache_dir/$tarball_name"

if [ "$force" = "1" ] || [ ! -s "$tarball" ]; then
    require_command curl
    echo "Downloading firmware: $asset_url" >&2
    curl -fL "$asset_url" -o "$tarball"
else
    echo "Using cached firmware: $tarball" >&2
fi

bundle_name="${tarball_name%.tar.gz}"
bundle_dir="$cache_dir/$bundle_name"
if [ "$force" = "1" ] || [ ! -d "$bundle_dir" ]; then
    rm -rf "$bundle_dir"
    tar -xzf "$tarball" -C "$cache_dir"
fi

required_files=(
    "$bundle_dir/bootloader.bin"
    "$bundle_dir/partition-table.bin"
    "$bundle_dir/ota_data_initial.bin"
    "$bundle_dir/serialite.bin"
)
for file in "${required_files[@]}"; do
    if [ ! -s "$file" ]; then
        echo "missing firmware artifact: $file" >&2
        exit 1
    fi
done

if [ -s "$bundle_dir/SHA256SUMS" ]; then
    (cd "$bundle_dir" && sha256sum -c SHA256SUMS --ignore-missing)
fi

flash_cmd=(
    "$python_bin" -m esptool
    --chip esp32s3
    -p "$port"
    -b "$baud"
    --before default_reset
    --after hard_reset
    write_flash
    --flash_mode dio
    --flash_freq 80m
    --flash_size 16MB
    0x0 bootloader.bin
    0x8000 partition-table.bin
    0xf000 ota_data_initial.bin
    0x20000 serialite.bin
)

if [ "$dry_run" = "1" ]; then
    printf 'cd %q\n' "$bundle_dir"
    printf '%q ' "${flash_cmd[@]}"
    printf '\n'
    exit 0
fi

cd "$bundle_dir"
exec "${flash_cmd[@]}"
