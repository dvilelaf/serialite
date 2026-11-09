# Device label

Text to print on the device, a rack tag or an enclosure insert. It contains
no secrets: never print passwords, fingerprints or server names.

## Front

```text
Serialite
Serial rescue console
WiFi: Serialite (press BOOT for the password)
http://192.168.4.1
Privileged physical console
```

## Back

```text
Use only on its own local WiFi. Do not expose to the Internet.
No HDMI, no remote power control, no virtual media.
BOOT: wake / show password      BOOT 10 s: factory reset
PWR: lock web input             PWR 3 s: power off
```

## Rack tag

```text
1. Press BOOT and join the WiFi network shown (or scan the QR).
2. Open http://192.168.4.1
3. Log in to the server on the terminal.
4. Tap PWR to lock the web console in an emergency.
```

## Enclosure

- Keep the USB-C port and both buttons reachable; label BOOT as `BOOT / WAKE / RESET` and PWR as `PWR / LOCK / OFF`.
- Do not cover the screen.
- Use a tamper-evident tag in shared racks.

## Custody

- Rotate the WiFi password from the web console when the device changes hands.
- Factory reset (BOOT 10 s) before reusing or retiring a unit.
