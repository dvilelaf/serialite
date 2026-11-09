#!/usr/bin/env bash
# Downloads a Serialite release, verifies it and flashes it over USB with esptool.
#
#   curl -fsSL https://github.com/dvilelaf/serialite/releases/latest/download/flash-latest-firmware.sh | bash
#
# The tarball is checked against the .sha256 asset published next to it in the
# same release, then against the SHA256SUMS inside it. Offsets come from the
# bundle's flash_args, written by the build itself.
set -euo pipefail

repo="dvilelaf/serialite"
version="latest"
port="${SERIALITE_FLASH_PORT:-}"
baud="${SERIALITE_FLASH_BAUD:-460800}"
python_bin="${SERIALITE_PYTHON:-python3}"
assume_yes=0
dry_run=0
cache_dir="${SERIALITE_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/serialite/firmware}"
release_base="${SERIALITE_RELEASE_BASE_URL:-https://github.com/${repo}/releases}"
dev_dir="${SERIALITE_DEV_DIR:-/dev}"
by_id_dir="${SERIALITE_BY_ID_DIR:-${dev_dir}/serial/by-id}"
curl_proto="=https"
if [ "${SERIALITE_TEST_MODE:-0}" = "1" ]; then
    curl_proto="=https,file"
fi

usage() {
    cat <<'EOF'
Usage:
  flash-latest-firmware.sh [--version vX.Y.Z] [--port /dev/ttyACM<N>] [--yes] [--dry-run]

Downloads a Serialite release bundle (cached), verifies its checksums and
flashes it with esptool. Without --port it looks for a single Espressif USB
device and asks before flashing it.
EOF
}

die() {
    echo "$*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port|-p)
            [ "$#" -ge 2 ] || die "missing value for --port"
            port="$2"
            shift 2
            ;;
        --version)
            [ "$#" -ge 2 ] || die "missing value for --version"
            version="$2"
            shift 2
            ;;
        --yes|-y)
            assume_yes=1
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
            usage >&2
            die "unknown argument: $1"
            ;;
    esac
done

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "$1 not found"
}

is_device_node() {
    if [ "${SERIALITE_TEST_MODE:-0}" = "1" ]; then
        [ -e "$1" ]
    else
        [ -c "$1" ]
    fi
}

download() {
    local url="$1" dest="$2"
    curl --proto "$curl_proto" --tlsv1.2 -fsSL "$url" -o "$dest.part"
    mv "$dest.part" "$dest"
}

resolve_latest_version() {
    local effective
    effective="$(curl --proto "$curl_proto" --tlsv1.2 -fsSL -o /dev/null -w '%{url_effective}' "${release_base}/latest")"
    printf '%s\n' "${effective##*/}"
}

autodetect_port() {
    local candidates="" path resolved count
    for path in "${by_id_dir}"/usb-Espressif_USB_JTAG_serial_debug_unit_*; do
        [ -e "$path" ] || continue
        resolved="$(readlink -f "$path")"
        is_device_node "$resolved" || continue
        case "
${candidates}
" in
            *"
${resolved}
"*) ;;
            *) candidates="${candidates}
${resolved}" ;;
        esac
    done
    count="$(printf '%s\n' "$candidates" | sed '/^$/d' | wc -l | tr -d ' ')"
    if [ "$count" = "0" ]; then
        die "No Espressif USB serial device found. Plug Serialite in, or pass --port /dev/ttyACM<N>."
    fi
    if [ "$count" != "1" ]; then
        echo "Multiple ESP32 serial devices found:" >&2
        printf '%s\n' "$candidates" | sed '/^$/d;s/^/  /' >&2
        die "Rerun with --port /dev/ttyACM<N> for the Serialite device."
    fi
    printf '%s\n' "$candidates" | sed '/^$/d'
}

confirm_port() {
    [ "$assume_yes" = "1" ] && return
    local answer=""
    if ! { exec 3</dev/tty; } 2>/dev/null; then
        die "Cannot ask for confirmation without a terminal; rerun with --port or --yes."
    fi
    printf 'Flash Serialite firmware to %s? Anything else on that port will be overwritten. [y/N] ' "$1" >/dev/tty
    read -r answer <&3 || true
    exec 3<&-
    case "$answer" in
        y|Y|yes|YES) ;;
        *) die "Aborted." ;;
    esac
}

require_command curl
require_command sha256sum
require_command tar

if [ "$version" = "latest" ]; then
    version="$(resolve_latest_version)"
fi
printf '%s\n' "$version" | grep -Eq '^v?[0-9A-Za-z._-]+$' || die "invalid release version: $version"

autodetected=0
if [ -z "$port" ]; then
    port="$(autodetect_port)"
    autodetected=1
    echo "Selected port: $port" >&2
elif ! is_device_node "$port"; then
    die "port is not a character device: $port"
fi

tarball="serialite-${version}.tar.gz"
checksum="${tarball}.sha256"
mkdir -p "$cache_dir"
cd "$cache_dir"
if [ ! -s "$tarball" ] || [ ! -s "$checksum" ]; then
    echo "Downloading Serialite ${version}" >&2
    download "${release_base}/download/${version}/${checksum}" "$checksum"
    download "${release_base}/download/${version}/${tarball}" "$tarball"
else
    echo "Using cached firmware: $cache_dir/$tarball" >&2
fi

grep -Eq "^[0-9a-f]{64}  ${tarball}\$" "$checksum" || die "unexpected checksum file format in $checksum"
if ! sha256sum --quiet -c "$checksum"; then
    rm -f "$tarball" "$checksum"
    die "checksum mismatch for $tarball; the cached copy was removed, rerun to download it again"
fi

bundle_name="serialite-${version}"
while IFS= read -r entry; do
    case "$entry" in
        "$bundle_name"/*) ;;
        *) die "unexpected path in $tarball: $entry" ;;
    esac
    case "/$entry/" in
        */../*) die "unexpected path in $tarball: $entry" ;;
    esac
done < <(tar -tzf "$tarball")

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
tar -xzf "$tarball" -C "$workdir" --no-same-owner --no-same-permissions
bundle="$workdir/$bundle_name"
(cd "$bundle" && sha256sum --quiet -c SHA256SUMS) || die "bundle contents do not match SHA256SUMS"
[ -s "$bundle/flash_args" ] || die "bundle has no flash_args"
if grep -q '^Profile: prod' "$bundle/manifest.txt" 2>/dev/null; then
    die "This is a production (Secure Boot) bundle; provision it as described in its INSTALL.md, not with this script."
fi

flash_cmd=(
    "$python_bin" -m esptool --chip esp32s3 -p "$port" -b "$baud"
    --before default_reset --after hard_reset write_flash "@flash_args"
)

if [ "$dry_run" = "1" ]; then
    printf 'cd %q\n' "$bundle"
    printf '%q ' "${flash_cmd[@]}"
    printf '\n'
    sed 's/^/  /' "$bundle/flash_args"
    exit 0
fi

if [ "$autodetected" = "1" ]; then
    confirm_port "$port"
fi
cd "$bundle"
"${flash_cmd[@]}"
