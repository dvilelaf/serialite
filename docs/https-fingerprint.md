# Local HTTPS Fingerprint Mode

ESP32-KVM runs HTTP on the local AP by default. Local HTTPS is optional and must not be presented as conventional CA-trusted web security. The safe operating model is certificate pinning by a SHA-256 fingerprint shown on the device display.

## Default

- HTTPS is disabled by default.
- HTTP remains available only on the local AP threat model.
- The firmware must not ask operators to ignore browser warnings blindly.
- The fingerprint must not be exported in config as a substitute for local verification.

## Enable Policy

HTTPS may only be enabled when all conditions are true:

- The operator explicitly requests HTTPS.
- A local certificate/key pair is present.
- The SHA-256 certificate fingerprint is displayed on the AMOLED.
- The operator confirms that the browser certificate fingerprint matches the on-device fingerprint.

If any condition is missing, HTTPS must fail closed and leave the current HTTP/AP behavior unchanged.

## Fingerprint Format

Use uppercase SHA-256 bytes separated by colons:

```text
AA:BB:CC:DD:...:11:22
```

This is intentionally easier to compare manually than raw base64 or ungrouped hex.

## Operator Runbook

1. Connect to the `KVM` AP.
2. Open the HTTPS URL only if the device screen shows an HTTPS fingerprint.
3. Inspect the browser certificate fingerprint.
4. Continue only if it exactly matches the AMOLED fingerprint.
5. If there is no match, stop and use AP-only HTTP in a controlled physical environment or rotate the local certificate.

## Current Implementation Status

The current firmware includes host-tested fingerprint formatting and an enable policy gate. It does not start an HTTPS listener yet. This avoids shipping a misleading half-enabled TLS flow; any future listener must pass this policy and show the fingerprint locally before use.
