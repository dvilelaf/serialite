# Serialite

Emergency serial console for headless Linux servers.

Serialite plugs into a server over USB, creates its own WiFi network, and gives you a local web terminal when SSH or networking is broken.

It is not a video KVM: no HDMI, no remote HID, no virtual media.

## Quickstart

If your device is not already flashed, download the latest firmware bundle from:

```text
https://github.com/dvilelaf/serialite/releases/latest
```

The bundle contains the ESP32 binaries, flash offsets, checksums, and host setup script.
To download and flash the latest firmware in one command:

```bash
curl -fsSL https://raw.githubusercontent.com/dvilelaf/serialite/main/tools/flash-latest-firmware.sh | bash -s -- --port /dev/ttyACM0
```

If you cloned the repo and have `just` installed:

```bash
just flash /dev/ttyACM0
```

Or download the bundle manually, extract it, and flash with:

```bash
python3 -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xf000 ota_data_initial.bin \
  0x20000 serialite.bin
```

1. Plug Serialite into the server USB port.

2. On the server, run:

   ```bash
   sudo sh -c 'curl -4fsSL -H "Accept: application/vnd.github.raw" "https://api.github.com/repos/dvilelaf/serialite/contents/tools/host/setup-linux-serial-console.sh?ref=main" | sh'
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
- `PWR` tap: lock or unlock terminal input.
- `PWR` hold: power off Serialite.
- Use Serialite only in physically controlled environments.
- Anyone with the WiFi password can reach the local terminal transport.

Development docs: [`docs/development.md`](docs/development.md).
