# Optional WiFi Client Mode

ESP32-KVM is designed to be reachable through its own local AP first. Optional WiFi client/STA mode is treated as an operational exception because joining an infrastructure network expands the attack surface beyond the physically local AP.

## Default

- WiFi client mode is disabled by default.
- The AP remains the primary management path.
- Config export/import must not carry infrastructure WiFi passwords.
- No automatic roaming, captive portal handling, cloud callback, or internet exposure is allowed.

## Enable Policy

The firmware policy only permits future STA enablement when all of these are true:

- A user explicitly requests WiFi client mode.
- Physical presence is proven, for example via local button or local pairing flow.
- The operator acknowledges that infrastructure WiFi increases exposure.
- NVS encryption is active before storing any STA secret.
- The local AP remains enabled as a recovery path.
- SSID and password are bounded and non-empty; the password must be production length.

If any condition fails, the feature must fail closed and leave the AP-only mode unchanged.

## Deployment Guidance

- Prefer AP-only operation in racks and break-glass scenarios.
- Use client mode only for controlled maintenance networks with firewall rules scoped to the device.
- Do not bridge traffic between STA and the USB console host.
- Do not expose the web UI to corporate LANs without HTTPS/fingerprint and a written exception.
- Rotate STA credentials when the device changes custody.

## Current Implementation Status

The current firmware includes a host-tested policy gate for STA mode. It does not initiate STA connections yet. This is intentional: the secure default is AP-only, and any future runtime implementation must pass the policy before touching `esp_wifi` station mode or persisting infrastructure credentials.
