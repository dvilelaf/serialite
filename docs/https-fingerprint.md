# Local HTTPS Fingerprint Mode

Serialite runs HTTP on the local AP by default. Local HTTPS is optional and must not be presented as conventional CA-trusted web security. The safe operating model is certificate pinning by a SHA-256 fingerprint shown on the device display.

## Default

- HTTPS is disabled by default.
- Operators enable it at build/config time with `CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE=y`.
- HTTP remains available only on the local AP threat model.
- The firmware must not ask operators to ignore browser warnings blindly.
- The fingerprint must not be exported in config as a substitute for local verification.

## Enable Policy

The HTTPS listener may only be started when all pre-start conditions are true:

- The operator explicitly requests HTTPS.
- A local certificate/key pair is present.
- The SHA-256 certificate fingerprint is displayed on the AMOLED.

The operator confirmation happens after the listener is up, inside the browser certificate viewer:

- The browser certificate SHA-256 fingerprint must exactly match the AMOLED fingerprint.
- If it does not match, the operator must stop using that session.

If identity generation is missing, HTTPS is not advertised and HTTP/AP fallback remains available. If a fingerprint was displayed but the HTTPS listener cannot start, startup fails closed instead of advertising a stale HTTPS URL.

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

## Runtime Implementation Status

When `CONFIG_ESP32_KVM_HTTPS_LOCAL_ENABLE=y`, the firmware generates an in-memory, per-boot ECDSA P-256 self-signed certificate with `CN=kvm.local` and `subjectAltName=dNSName:kvm.local` when local entropy is available. It computes the SHA-256 fingerprint over the DER certificate, shows the fingerprint on the AMOLED with `https://kvm.local`, and starts `esp_https_server` on port 443 only after the policy gate accepts the displayed fingerprint state.

If identity generation fails before any fingerprint is displayed, the device falls back to the existing local HTTP/AP behavior so rescue access remains available. In that fallback state, the screen and mDNS must not advertise HTTPS. If the fingerprint was displayed but HTTPS startup later fails, the web startup fails closed instead of advertising a stale HTTPS URL.

The stack identity buffer is zeroized after server startup. `esp_https_server` keeps an internal copy of the private key for the lifetime of the HTTPS server; production zeroization requirements must treat that server context as containing live key material until shutdown.
