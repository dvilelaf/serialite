# HTTPS Product Strategy

## Decision

ESP32-KVM has two security profiles:

- **Rescue default**: local AP, WPA2, HTTP WebUI, physical-presence workflow, maximum availability.
- **Critical production**: HTTPS required for terminal/OTA unless an operator explicitly enters a documented break-glass HTTP mode.

Self-signed HTTPS must not be the default consumer flow. Browser warnings, certificate exceptions, and manual SHA-256 comparison are not a premium rescue experience under incident pressure.

## Why Local HTTPS Is Hard

Browsers show a secure lock only when the certificate chains to a trusted CA and the URL hostname matches the certificate SAN. A per-boot self-signed certificate for `kvm.local` can encrypt traffic, but the browser cannot know it is the right device unless the operator validates the fingerprint or installs a trusted root/certificate.

That means these are different things:

- `http://192.168.4.1`: reliable rescue path, browser marks it not secure.
- `https://kvm.local` with self-signed cert: encrypted, but browser warns until trust is established.
- CA-trusted `https://kvm-<device-id>.<domain>`: browser-trusted HTTPS without warnings.

## Consumer-Grade HTTPS Target

The target for a production-grade product is a stable per-device HTTPS identity:

1. Each device has a unique hostname, for example `kvm-<device-id>.vendor.example`.
2. The AP DNS resolver maps that hostname to `192.168.4.1`.
3. The device stores a provisioned private key and certificate in protected storage.
4. The browser opens the stable HTTPS URL from QR/manual entry.
5. The device displays transport mode and certificate expiry locally.
6. Certificate renewal and factory reset have explicit product flows.

For enterprise deployments, an alternative is an organization-installed trust root via MDM, with device certificates signed by that private CA.

## Advanced Fingerprint Mode

The existing local fingerprint mode remains an expert/admin mode, not the default:

- It uses `https://kvm.local`, not raw IP.
- It only advertises HTTPS when the TLS listener is actually active.
- It requires the fingerprint to be visible locally before trust is claimed.
- It must never ask users to blindly bypass browser warnings.

## Rules

- Do not enable local self-signed HTTPS by default.
- Do not fail AP/WebUI rescue startup because TLS identity generation failed.
- Do not change the AMOLED or WebUI layout as a side effect of transport work.
- Do not serve a trust-root installation flow from the same unauthenticated AP and call it secure.
- Keep HTTP as the availability-first rescue default until a real trust/provisioning model exists.

## Required Tests Before HTTPS Promotion

- Browser harness for HTTPS with untrusted cert: must demonstrate the browser blocks or warns.
- Browser harness for trusted HTTPS: must load `/terminal`, use `wss://`, and show no console/page errors.
- Host test that HTTPS cookies include `Secure` when TLS is active.
- Host test that HTTPS is advertised only when the HTTPS listener is active.
- Hardware/manual test that browser certificate fingerprint matches the device-displayed fingerprint.
- Memory/stack guard for `/terminal` so WebUI rendering cannot depend on large contiguous heap or stack buffers.
