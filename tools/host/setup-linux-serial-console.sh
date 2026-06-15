#!/usr/bin/env sh
set -eu

UNIT="esp32-kvm-serial-console.service"
SYMLINK_NAME="esp32-kvm-console"
RULE_NAME="99-esp32-kvm.rules"

dev_dir="${ESP32_KVM_DEV_DIR:-/dev}"
by_id_dir="${ESP32_KVM_BY_ID_DIR:-${dev_dir}/serial/by-id}"
etc_dir="${ESP32_KVM_ETC_DIR:-/etc}"
rule_dir="${etc_dir}/udev/rules.d"
systemd_dir="${etc_dir}/systemd/system"
rule_path="${rule_dir}/${RULE_NAME}"
service_path="${systemd_dir}/${UNIT}"
symlink_path="${dev_dir}/${SYMLINK_NAME}"

usage() {
    cat <<'EOF'
Usage:
  setup-linux-serial-console.sh [--device /dev/ttyACM<N>]
  setup-linux-serial-console.sh --uninstall

Enables a persistent Linux serial login on the ESP32-KVM USB serial device.

Recommended install from a tagged release:
  curl -fsSL https://raw.githubusercontent.com/YOUR_ORG/esp32-kvm/vX.Y.Z/tools/host/setup-linux-serial-console.sh | sudo sh
EOF
}

device=""
uninstall=0
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
        --uninstall)
            uninstall=1
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

is_root() {
    case "${ESP32_KVM_ASSUME_ROOT:-}" in
        1) return 0 ;;
        0) return 1 ;;
    esac
    [ "$(id -u)" -eq 0 ]
}

require_root() {
    if is_root; then
        return
    fi
    cat >&2 <<'EOF'
This setup changes systemd and udev state, so it must run as root.

Use:
  curl -fsSL https://raw.githubusercontent.com/YOUR_ORG/esp32-kvm/vX.Y.Z/tools/host/setup-linux-serial-console.sh | sudo sh

Or download a tagged release, review this script, then run:
  sudo sh ./setup-linux-serial-console.sh
EOF
    exit 1
}

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

is_device_node() {
    if [ "${ESP32_KVM_TEST_MODE:-0}" = "1" ]; then
        [ -e "$1" ]
    else
        [ -c "$1" ]
    fi
}

canonical_path() {
    readlink -f "$1"
}

autodetect_device() {
    candidates=""
    for path in "${by_id_dir}"/usb-Espressif_USB_JTAG_serial_debug_unit_*; do
        [ -e "$path" ] || continue
        resolved="$(canonical_path "$path")"
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
        cat >&2 <<EOF
No ESP32-KVM serial device found.

Plug the device in and retry. If auto-detection is not available on this host,
rerun with:
  sudo sh ./setup-linux-serial-console.sh --device /dev/ttyACM<N>
EOF
        exit 1
    fi
    if [ "$count" != "1" ]; then
        echo "Multiple ESP32 serial devices found:" >&2
        printf '%s\n' "$candidates" | sed '/^$/d;s/^/  /' >&2
        echo "Rerun with --device /dev/ttyACM<N> for the ESP32-KVM device." >&2
        exit 1
    fi

    printf '%s\n' "$candidates" | sed '/^$/d'
}

resolve_device() {
    if [ -n "$device" ]; then
        if ! is_device_node "$device"; then
            echo "device is not a character device: $device" >&2
            exit 1
        fi
        canonical_path "$device"
        return
    fi

    autodetect_device
}

udev_properties() {
    udevadm info -q property -n "$1"
}

property_value() {
    key="$1"
    properties="$2"
    printf '%s\n' "$properties" | sed -n "s/^${key}=//p" | sed -n '1p'
}

write_rule() {
    selected_device="$1"
    properties="$(udev_properties "$selected_device")"
    vendor_id="$(property_value ID_VENDOR_ID "$properties")"
    model_id="$(property_value ID_MODEL_ID "$properties")"
    serial_short="$(property_value ID_SERIAL_SHORT "$properties")"

    if [ -z "$vendor_id" ] || [ -z "$model_id" ] || [ -z "$serial_short" ]; then
        cat >&2 <<EOF
Could not read stable USB identity for ${selected_device}.

Refusing to install a persistent login console without VID, PID, and serial.
EOF
        exit 1
    fi
    validate_usb_identity

    mkdir -p "$rule_dir"
    cat >"$rule_path" <<EOF
# Managed by ESP32-KVM. Creates a stable tty name for the rescue serial console.
SUBSYSTEM=="tty", ATTRS{idVendor}=="${vendor_id}", ATTRS{idProduct}=="${model_id}", ATTRS{serial}=="${serial_short}", SYMLINK+="${SYMLINK_NAME}"
EOF

    echo "Selected device: ${selected_device}"
    echo "Stable device: ${symlink_path}"
    echo "USB identity: ${vendor_id}:${model_id} serial ${serial_short}"
}

safe_identity_value() {
    name="$1"
    value="$2"
    pattern="$3"
    if ! printf '%s\n' "$value" | grep -Eq "$pattern"; then
        echo "unsafe USB identity value for ${name}: ${value}" >&2
        exit 1
    fi
}

validate_usb_identity() {
    safe_identity_value "ID_VENDOR_ID" "$vendor_id" '^[0-9a-fA-F]{4}$'
    safe_identity_value "ID_MODEL_ID" "$model_id" '^[0-9a-fA-F]{4}$'
    safe_identity_value "ID_SERIAL_SHORT" "$serial_short" '^[A-Za-z0-9._:-]+$'
}

wait_for_stable_symlink() {
    selected_device="$1"
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if [ -e "$symlink_path" ]; then
            resolved="$(canonical_path "$symlink_path")"
            if [ "$resolved" = "$selected_device" ]; then
                return
            fi
            echo "stable symlink points to ${resolved}, expected ${selected_device}" >&2
            exit 1
        fi
        sleep 0.2
    done

    echo "udev did not create ${symlink_path}; refusing to continue" >&2
    exit 1
}

write_service() {
    agetty_path="$(command -v agetty || true)"
    if [ -z "$agetty_path" ]; then
        echo "agetty not found; install util-linux before running host setup" >&2
        exit 1
    fi

    mkdir -p "$systemd_dir"
    cat >"$service_path" <<EOF
[Unit]
Description=ESP32-KVM rescue serial console
Documentation=https://github.com/YOUR_ORG/esp32-kvm
After=systemd-udev-settle.service

[Service]
Type=idle
ExecStart=-${agetty_path} -o '-p -- \\\\u' --keep-baud 115200,57600,38400,9600 /dev/${SYMLINK_NAME} vt220
Restart=always
RestartSec=1
UtmpIdentifier=${SYMLINK_NAME}
TTYPath=/dev/${SYMLINK_NAME}
TTYReset=yes
TTYVHangup=yes
KillSignal=SIGHUP
IgnoreSIGPIPE=no
SendSIGHUP=yes

[Install]
WantedBy=getty.target
EOF
}

reload_udev() {
    udevadm control --reload-rules
    udevadm trigger --subsystem-match=tty >/dev/null 2>&1 || true
    udevadm settle >/dev/null 2>&1 || true
}

install_console() {
    if ! command_exists systemctl; then
        echo "systemd is required; systemctl not found" >&2
        exit 1
    fi
    if ! command_exists udevadm; then
        echo "udev is required; udevadm not found" >&2
        exit 1
    fi

    selected_device="$(resolve_device)"
    write_rule "$selected_device"
    reload_udev
    wait_for_stable_symlink "$selected_device"
    write_service
    systemctl daemon-reload
    systemctl enable --now "$UNIT"

    if ! systemctl is-active --quiet "$UNIT"; then
        echo "$UNIT was enabled but is not active" >&2
        systemctl --no-pager --full status "$UNIT" >&2 || true
        exit 1
    fi

    cat <<EOF

ESP32-KVM host serial console is ready.

Service: ${UNIT}
Device:  ${symlink_path}

Join WiFi KVM and open http://192.168.4.1

Undo:
  curl -fsSL https://raw.githubusercontent.com/YOUR_ORG/esp32-kvm/vX.Y.Z/tools/host/setup-linux-serial-console.sh | sudo sh -s -- --uninstall
EOF
}

uninstall_console() {
    if command_exists systemctl; then
        systemctl disable --now "$UNIT" >/dev/null 2>&1 || true
    fi
    rm -f "$rule_path" "$service_path" "$symlink_path"
    if command_exists systemctl; then
        systemctl daemon-reload >/dev/null 2>&1 || true
    fi
    if command_exists udevadm; then
        udevadm control --reload-rules >/dev/null 2>&1 || true
        udevadm trigger --subsystem-match=tty >/dev/null 2>&1 || true
    fi
    echo "ESP32-KVM host serial console removed."
}

require_root
if [ "$uninstall" = "1" ]; then
    uninstall_console
else
    install_console
fi
