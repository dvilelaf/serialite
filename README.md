# Serialite

Emergency serial console for headless Linux servers.

Serialite plugs into a server over USB, creates its own WiFi network, and gives you a local web terminal when SSH or networking is broken.

It is not a video KVM: no HDMI, no remote HID, no virtual media.

## Quickstart

1. Plug Serialite into the server USB port.

2. On the server, run:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/dvilelaf/serialite/main/tools/host/setup-linux-serial-console.sh | sudo sh
   ```

3. Join the WiFi network:

   ```text
   SSID: KVM
   Password: shown on the Serialite screen
   ```

4. Open:

   ```text
   http://192.168.4.1
   ```

5. Use the terminal.

## Notes

- `BOOT`: wake screen and reveal the WiFi password.
- `PWR` for 3 seconds: lock or unlock terminal input.
- Use Serialite only in physically controlled environments.
- Anyone with the WiFi password can reach the local terminal transport.

Development docs: [`docs/development.md`](docs/development.md).
