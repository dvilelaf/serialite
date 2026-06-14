# Security Test Matrix

This matrix maps the minimum automated security coverage for the rescue-console firmware. It is intentionally host-testable so regressions are caught without hardware.

Run:

```bash
source /home/david/esp-idf/export.sh
./scripts/verify.sh
```

## Covered Controls

| Control | Automated evidence |
| --- | --- |
| Auth/session required for sensitive flows | `tests/host/test_web_security.c`, `tests/host/test_http_route_policy.c` |
| CSRF and same-origin requirement for mutating routes | `tests/host/test_web_security.c`, `tests/host/test_http_route_policy.c` |
| WebSocket origin and authenticated write control | `tests/host/test_web_security.c`, `tests/host/test_web_input_policy.c`, `tests/host/test_http_route_policy.c` |
| Login rate limit and HTTP request budget | `tests/host/test_web_security.c`, `tests/host/test_http_rate_limit.c` |
| WebSocket frame size and byte/frame rate limits | `tests/host/test_web_input_policy.c` |
| Route allow-list and request body limits | `tests/host/test_http_route_policy.c` |
| Credential generation and rotation policy | `tests/host/test_credentials.c`, `tests/host/test_web_security.c`, `tests/host/test_http_route_policy.c` |
| Corrupt or incomplete WiFi config fails closed | `tests/host/test_storage_config.c`, `tests/host/test_startup_policy.c` |
| Secret persistence requires encrypted NVS | `tests/host/test_storage_config.c` |
| Persisted web auth uses hash+salt, not plaintext | `tests/host/test_storage_config.c`, `tests/host/test_web_security.c` |
| Legacy plaintext secret scrub policy | `tests/host/test_storage_config.c` |
| Local initial pairing code format, one-time consume and lockout | `tests/host/test_local_pairing.c` |
| Config export/import omits secrets and validates schema/checksum | `tests/host/test_config_transfer.c`, `tests/host/test_http_route_policy.c` |
| Emergency lock gesture and session invalidation primitive | `tests/host/test_emergency_lock_gesture.c`, `tests/host/test_web_security.c` |
| Diagnostics export redacts sensitive strings | `tests/host/test_diagnostics_export.c` |

## Non-Automated Hardware Checks

- AP appears as WPA2-protected `KVM`.
- The device enumerates as ESP32-S3 USB-Serial/JTAG on `/dev/ttyACM0`.
- `http://kvm.local` resolves only when the client is connected to the AP and supports mDNS.
- Credential rotation shows new secrets on the AMOLED only, not in the HTTP response.
- First web login rejects missing or incorrect local pair code, then accepts the one-time code shown on the AMOLED.
- `/config.json` must not contain WiFi password, web password hash, salts or serial transcript data.
- Old web sessions are rejected after credential rotation.
- After credential rotation and reboot, the AP requires the rotated WiFi password and web login accepts the rotated web password.
- BOOT long-press behavior must be smoke-tested on the physical board after firmware changes touching reset or lock flows.
