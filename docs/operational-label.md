# ESP32-KVM Operational Label

This document defines the text that should be printed on the device label, rack tag, or enclosure insert.
It intentionally contains **No secrets**: do not print passwords, pair codes, private keys, fingerprints that are not regenerated with the device, or customer/server identifiers.

## Front Label

```text
ESP32-KVM
Serial rescue console
SSID: KVM
URL: http://192.168.4.1
Privileged physical console
```

## Rear / Bottom Label

```text
Use only on trusted local AP.
Do not expose to Internet.
No HDMI KVM, no power control, no virtual media.
BOOT 3s: emergency lock
BOOT 10s: factory reset
```

## Rack Runbook Tag

```text
1. Connect nearby laptop/phone to SSID: KVM.
2. Open http://192.168.4.1 or scan the screen QR.
3. Open the local web console.
4. Authenticate in the Linux serial login when prompted.
5. Sign out or hold BOOT for emergency session lock.
```

## Enclosure Requirements

- Keep USB-C port and both physical buttons accessible.
- Label the BOOT button as `BOOT / LOCK / RESET`.
- Label the other button as `WAKE` if it is used to wake the AMOLED.
- Do not cover the AMOLED QR/status area.
- Use a tamper-evident asset tag if deployed in shared racks.
- Prefer matte, high-contrast text readable under rack lighting.

## Security Notes

- Treat this device as privileged physical console access.
- Store printed labels without credentials; rotate credentials via the web UI when device custody changes.
- If a unit is lost or removed from service, factory reset it before reuse.
- If the label is damaged or unreadable, replace it before production deployment.
