# Security

## Threat model

Serialite exposes a Linux login prompt over WiFi. Whoever controls the
browser session can type on the server's console, so the design question is
who can reach that session.

In scope:

- people within WiFi range who do not know the password
- web pages the operator visits while joined to the Serialite AP (cross-site
  requests, DNS rebinding)
- someone who saw an old password, or held the device in the past
- a malicious or broken firmware image uploaded over OTA

Out of scope: an attacker with prolonged physical access to a lab build (they
can read the flash), and the security of the server's own accounts.

## Who gets in

1. **WiFi.** WPA2/WPA3 (SAE with protected management frames for WPA3
   clients), one client at a time. The password is six words from the EFF
   large list (about 77 bits), generated on the device with the hardware RNG
   and shown only on its screen after pressing BOOT. The WiFi driver keeps it
   in RAM (`WIFI_STORAGE_RAM`, `CONFIG_ESP_WIFI_NVS_ENABLED=n`).
2. **Web session.** There is no separate web password: the WiFi password and
   physical access to the screen are the factor. A session is created only by
   `POST /api/session` with an `Origin` matching the device, so other sites
   cannot create or replace it. A new session replaces the previous one and
   closes its terminal connections. Sessions expire after 15 idle minutes.
3. **Server login.** The serial getty asks for the server's own credentials.

## Web protections

Every route is declared in one table in `web_server.c` with an access level
(public, session, session + same Origin + CSRF token), a body limit and
whether the emergency lock blocks it. The dispatcher reads all headers
first, asks `web_guard_evaluate()` and, on rejection, replies and returns
without running the handler (closing the connection if a body was sent).

- **Host allowlist** (`192.168.4.1`, `serialite.local`): requests for any
  other name get 421, which defeats DNS rebinding.
- **Origin allowlist** on every state-changing request and the WebSocket upgrade.
- **CSRF token** per session, sent in the `X-CSRF-Token` header and compared in constant time.
- **Cookie** `serialite_session`: `HttpOnly; SameSite=Strict` (plus `Secure` under HTTPS).
- **CSP** `script-src 'self'` with no inline scripts; pages are static files
  and data comes from the JSON API. `frame-ancestors 'none'`, `nosniff`, no referrer.
- **Rate limit** of 40 requests per second, and per-frame size and rate
  limits on terminal input. Large pastes need confirmation.
- **Terminal output filter** drops OSC/DCS/APC strings (clipboard writes,
  hyperlinks) and queries that would make the browser terminal answer back
  into the shell.
- **Emergency lock** (PWR tap or the web button) ends the session, closes
  terminals and refuses new sessions until PWR is tapped again.

## Secrets

| Secret | Where it lives | When it changes |
|---|---|---|
| WiFi password | RAM; RTC memory across software restarts; encrypted NVS only in production builds after a rotation or import | power cycle (lab), rotation, factory reset |
| TLS key (HTTPS builds) | RAM, regenerated every boot, copied into esp_https_server | every boot |
| Session and CSRF tokens | RAM in the httpd task | every session |

Secrets are never logged, exported in the config JSON or included in
diagnostics; stack copies are wiped after use. Secrets are generated with
`bootloader_random_enable()` because they are made before WiFi starts. A
factory reset erases the whole NVS partition and the RTC copy.

## OTA

Uploads need the session, the same Origin and the CSRF token. The image head
must be an ESP-IDF app with this project's name. In production builds the
bootloader also verifies the Secure Boot signature and refuses images whose
`secure_version` is lower than the device's (anti-rollback). A new image is
only confirmed after the serial bridge, AP and web server start; otherwise
the bootloader rolls back on the next reset.

## Build profiles

**Lab** (`sdkconfig.defaults`, what releases ship): no Secure Boot, no flash
encryption, HTTP, nothing persisted. Anyone holding the board can read or
replace its firmware. Fine for a device you keep with you; not for one left
in a shared rack.

**Production** (`sdkconfig.prod.defaults` on top): Secure Boot v2, release-mode
flash encryption (also disables JTAG), encrypted NVS with keys in the
`nvs_keys` partition, anti-rollback, HTTPS with the certificate fingerprint on
screen, and its own partition table (`partitions-prod.csv`, table at 0xC000
because the signed bootloader does not fit below 0x8000).
`scripts/lint_production_profile.py` checks all of this. The first boot burns
eFuses: test every profile change on a sacrificial board.

With HTTPS, compare the SHA-256 the browser shows for the certificate with
the one on the screen (press BOOT twice) before typing anything.

## Known limits

- esp_http_server has one task. An OTA upload occupies it (at most five
  minutes) and terminal output waits meanwhile; output beyond the queue is
  dropped and counted in diagnostics.
- The request rate limit is global rather than per client. The AP admits one
  client, so in practice that is the same thing.
- Lab builds keep the firmware and, after a software restart, the current
  password readable to anyone who can attach a USB cable and read the flash.
- Release archives are checksummed, not signed. Anyone who can publish a
  GitHub release for this repository can publish firmware.

## How it is tested

| Property | Test |
|---|---|
| Rejected requests never reach handlers (OTA, import, rotation) | `tools/esp_http_harness/security_checks.py` against the real server on Linux |
| Host and Origin allowlists, session creation, CSRF, body limits, rate limit | same, plus `tests/host/test_web_guard.c`, `test_web_request.c` |
| WebSocket needs session and Origin; one operator at a time | `security_checks.py` |
| Emergency lock ends the session and blocks new ones | `security_checks.py`, Playwright `mobile-console.spec.js` |
| Session tokens, idle timeout, replacement | `tests/host/test_web_security.c` |
| Terminal output filter | `tests/host/test_web_terminal_ansi.c` |
| Passwords: unbiased choice, every word valid | `tests/host/test_credentials.c`, `scripts/gen_wordlist.py --check` |
| Which credentials are used at boot | `tests/host/test_credential_boot_policy.c` |
| OTA image identity | `tests/host/test_ota_update_policy.c` |
| Production profile and partition tables | `tests/test_lint_production_profile.py`, CI builds the profile |
| Release download integrity | `tests/test_flash_latest_firmware.py` |

Still checked by hand on hardware: the AP's WPA mode, the screen reveal and
QR, button gestures, factory reset and a production first boot.
