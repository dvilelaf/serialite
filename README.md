# Serialite

Emergency serial console for headless Linux servers.

Serialite plugs into a server over USB, creates a private WiFi AP, and exposes a local web terminal. It is for recovery when SSH or networking is broken.

This is not a video KVM. There is no HDMI capture, remote HID keyboard, or virtual media.

## Fastest Path

1. Plug Serialite into the server USB port.

2. On the server, enable the serial login service with the command from the release `INSTALL.md`.

   A released command has this shape:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/dvilelaf/serialite/vX.Y.Z/tools/host/setup-linux-serial-console.sh | sudo sh
   ```

   Use a tagged release URL, not `main`. If auto-detection fails, the release `INSTALL.md` also includes the `--device /dev/ttyACM<N>` form:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/dvilelaf/serialite/vX.Y.Z/tools/host/setup-linux-serial-console.sh | sudo sh -s -- --device /dev/ttyACM<N>
   ```

3. Join the WiFi network from your laptop or phone:

   ```text
   SSID: KVM
   Password: shown on the ESP32 screen
   ```

4. Open:

   ```text
   http://192.168.4.1
   ```

5. Use the terminal like a local Linux serial console.

## Host Setup

The setup script:

- creates `/dev/serialite-console` with a udev rule bound to the ESP32 USB identity;
- installs and enables `serialite-serial-console.service`;
- avoids binding the login console to unstable names like `/dev/ttyACM0`;
- refuses ambiguous auto-detection instead of guessing.

To remove it:

```bash
curl -fsSL https://raw.githubusercontent.com/dvilelaf/serialite/vX.Y.Z/tools/host/setup-linux-serial-console.sh | sudo sh -s -- --uninstall
```

## Buttons

- `BOOT`: wakes the screen and temporarily reveals the WiFi password.
- `PWR` for 3 seconds: locks or unlocks terminal input.
- `BOOT` for 10 seconds: factory-resets Serialite configuration.

## Web UI

- The main screen is the terminal.
- The `+` button opens quick actions: useful keys, TTY resize, diagnostics, WiFi rotation, and emergency lock.
- `Emergency lock` blocks web terminal input. To unlock it, hold `PWR` for 3 seconds.
- `Rotate WiFi` generates a new password, shows it on the ESP32 screen, and restarts the AP.

## Security Model

- WiFi WPA2 is the local access boundary.
- There is no separate web password. Authentication happens at the Linux serial login prompt.
- Anyone with the WiFi password and network access to the AP can reach the web terminal transport.
- Secrets are not served by HTTP; they are shown physically on the ESP32 screen.
- Use this in physically controlled environments and rotate WiFi credentials after rescue work.

## Firmware Releases

End-user firmware should be published as a release asset, not committed to the repo. A release bundle contains firmware binaries, a manifest, SHA256 checksums, and the host setup script.

Maintainers can build that bundle with:

```bash
SERIALITE_RAW_BASE_URL=https://raw.githubusercontent.com/dvilelaf/serialite ./scripts/package-release.sh --version vX.Y.Z
```

The bundle includes `INSTALL.md` with copy-paste commands for that exact release.

## Development

See [`docs/development.md`](docs/development.md).
