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
| Corrupt or incomplete WiFi config fails closed | `tests/host/test_storage_config.c`, `tests/host/test_startup_policy.c` |
| Secret persistence requires encrypted NVS | `tests/host/test_storage_config.c` |
| Legacy plaintext secret scrub policy | `tests/host/test_storage_config.c` |
| Emergency lock gesture and session invalidation primitive | `tests/host/test_emergency_lock_gesture.c`, `tests/host/test_web_security.c` |
| Diagnostics export redacts sensitive strings | `tests/host/test_diagnostics_export.c` |

## Non-Automated Hardware Checks

- AP appears as WPA2-protected `KVM`.
- The device enumerates as ESP32-S3 USB-Serial/JTAG on `/dev/ttyACM0`.
- `http://kvm.local` resolves only when the client is connected to the AP and supports mDNS.
- BOOT long-press behavior must be smoke-tested on the physical board after firmware changes touching reset or lock flows.
