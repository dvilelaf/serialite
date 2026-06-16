# Serialite

Emergency serial console for headless Linux servers.

Serialite plugs into a server over USB, creates its own WiFi network, and gives you a local web terminal when SSH or networking is broken.

It is not a video KVM: no HDMI, no remote HID, no virtual media.

## Quickstart

1. Flash Serialite if needed:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/dvilelaf/serialite/main/tools/flash-latest-firmware.sh | bash
   ```

2. Plug Serialite into the server USB port.

3. On the server, run:

   ```bash
   sudo sh -c 'curl -4fsSL -H "Accept: application/vnd.github.raw" "https://api.github.com/repos/dvilelaf/serialite/contents/tools/host/setup-linux-serial-console.sh?ref=main" | sh'
   ```

4. Join the WiFi network:

   ```text
   SSID: KVM
   Password: shown on the Serialite screen
   ```

5. Open:

   ```text
   http://192.168.4.1
   ```

6. Use the terminal.

## Notes

- `BOOT`: wake screen and reveal the WiFi password.
- `PWR` tap: lock or unlock terminal input.
- `PWR` hold: power off Serialite.
- Use Serialite only in physically controlled environments.
- Anyone with the WiFi password can reach the local terminal transport.

Development docs: [`docs/development.md`](docs/development.md).
