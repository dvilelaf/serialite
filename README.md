# Serialite

Emergency serial console for headless Linux servers.

Serialite plugs into a server's USB port, creates its own WiFi network and
gives you a web terminal on the server's serial console when SSH or the
network is broken. It runs on the Waveshare ESP32-S3-Touch-AMOLED-1.8.

It is not a video KVM: no HDMI, no remote keyboard or mouse, no virtual media.

## Quickstart

1. Flash the latest release (it verifies the download and asks before writing):

   ```bash
   curl -fsSL https://github.com/dvilelaf/serialite/releases/latest/download/flash-latest-firmware.sh | bash
   ```

2. Plug Serialite into the server's USB port.

3. On the server, set up the serial login. The script runs as root, so read it first:

   ```bash
   curl -fsSLO https://github.com/dvilelaf/serialite/releases/latest/download/setup-linux-serial-console.sh
   less setup-linux-serial-console.sh
   sudo sh setup-linux-serial-console.sh
   ```

4. Press BOOT on Serialite to show the WiFi password and QR code (press it once more if
   the screen was asleep), then join the network (SSID `Serialite`).

5. Open `http://192.168.4.1` and log in to the server on the terminal.

## Buttons

| Button | Action |
|---|---|
| BOOT | Wake the screen; when it is on, show the WiFi password and QR code for 30 s. With HTTPS, the next press shows the certificate fingerprint. |
| BOOT held 10 s | Factory reset: erases stored settings and generates a new WiFi password. |
| PWR tap | Emergency lock: ends the web session and refuses new ones until PWR is tapped again. |
| PWR held 3 s | Power off. |

## Security in one paragraph

Anyone who knows the WiFi password reaches the server's login prompt, so treat
Serialite as privileged physical access. The password is six random words
shown only on the device, the AP accepts one client at a time, and the web
console only answers same-origin requests to its own address. The server's
own login is the last barrier: keep account passwords strong. Release builds
are lab builds; see [`docs/security.md`](docs/security.md) for the threat
model and the production profile (Secure Boot, flash encryption, HTTPS).

## More

- [`docs/architecture.md`](docs/architecture.md): components, layering and porting to other boards.
- [`docs/security.md`](docs/security.md): threat model, web protections, secrets and production builds.
- [`docs/development.md`](docs/development.md): building, testing and releasing.
- [`docs/operational-label.md`](docs/operational-label.md): text for the device label.

Licensed under the MIT License; third-party code is listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
