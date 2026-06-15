#!/usr/bin/env sh
set -eu

usage() {
    cat <<'EOF'
Usage:
  sudo ./tools/host/setup-linux-serial-console.sh [--device /dev/ttyACM<N>]

Enables a persistent Linux serial login on the ESP32-KVM USB serial device.
Run this once on each server you want to rescue through ESP32-KVM.
EOF
}

device=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --device)
            if [ "$#" -lt 2 ]; then
                echo "missing value for --device" >&2
                exit 2
            fi
            device="$2"
            shift 2
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

if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        if [ -n "$device" ]; then
            exec sudo "$0" --device "$device"
        fi
        exec sudo "$0"
    fi
    echo "run as root or install sudo" >&2
    exit 1
fi

resolve_device() {
    if [ -n "$device" ]; then
        if [ ! -c "$device" ]; then
            echo "device is not a character device: $device" >&2
            exit 1
        fi
        readlink -f "$device"
        return
    fi

    candidates=""
    for path in /dev/serial/by-id/*Espressif* /dev/ttyACM*; do
        [ -e "$path" ] || continue
        [ -c "$path" ] || continue
        resolved="$(readlink -f "$path")"
        case "
$candidates
" in
            *"
$resolved
"*) ;;
            *) candidates="${candidates}
$resolved" ;;
        esac
    done

    count="$(printf '%s\n' "$candidates" | sed '/^$/d' | wc -l | tr -d ' ')"
    if [ "$count" = "0" ]; then
        echo "no ESP32-KVM serial device found; plug the device in and try again" >&2
        exit 1
    fi
    if [ "$count" != "1" ]; then
        echo "multiple serial devices found:" >&2
        printf '%s\n' "$candidates" | sed '/^$/d;s/^/  /' >&2
        echo "rerun with --device /dev/ttyACM<N>" >&2
        exit 1
    fi

    printf '%s\n' "$candidates" | sed '/^$/d'
}

if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemd is required; systemctl not found" >&2
    exit 1
fi

resolved_device="$(resolve_device)"
tty_name="$(basename "$resolved_device")"
unit="serial-getty@${tty_name}.service"

systemctl enable --now "$unit"

if ! systemctl is-active --quiet "$unit"; then
    echo "$unit was enabled but is not active" >&2
    systemctl --no-pager --full status "$unit" >&2 || true
    exit 1
fi

cat <<EOF
ESP32-KVM host serial console is ready.

Device: /dev/${tty_name}
Service: ${unit}

Join WiFi KVM and open http://192.168.4.1
EOF
