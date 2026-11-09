# Architecture

Serialite is an ESP-IDF 5.3 application split into small components. The
goal is that everything above the board layer can move unchanged to another
board, or become an app inside a larger firmware.

## Layers

```text
main/                      boot sequence and policy: which credentials, what to start
  app_main.c               wires components together; degrades instead of aborting
  app_credentials.c        stored / cached / generated AP credentials, rotation, import
  ap_guard.c               stops the AP after 10 idle minutes
  credential_boot_policy.c, ap_exposure_policy.c   pure decisions, host-tested

services (no board knowledge)
  terminal_bridge          byte pipe: serial output -> scrollback + subscribers,
                           operator input -> serial writer
  usb_console              USB Serial/JTAG reader/writer feeding terminal_bridge
  web_server               HTTP/WebSocket console, JSON API, static assets
  wifi_ap                  WPA2/WPA3 SoftAP
  network_identity         mDNS name
  storage, config_transfer encrypted-NVS settings and their JSON export
  ota_update               streaming OTA with image checks
  credentials              word-list passwords and WiFi QR payloads
  local_tls_identity, https_fingerprint   per-boot TLS certificate
  event_log, diagnostics_export, json_writer, app_watchdog

device
  board                    board.h / board_display.h, one source file per board
  buttons                  gesture detection -> callbacks
  lvgl_ui                  status screen; a stub on boards without a display
  secret_display           masking helpers for the screen
```

Dependencies only point downwards. `web_server` does not know about the
board, the display or the buttons; `lvgl_ui` and `buttons` do not know about
the web server. `main` connects them through callbacks:

- buttons -> `web_server_toggle_lock()`, `lvgl_ui_notify_*()`, factory reset, `board_power_off()`
- `lvgl_ui` pulls its status through a callback that `main` fills from the services
- `web_server` calls back into `main` to rotate credentials and import/export settings

## Concurrency rules

- **httpd task** owns everything in `web_server`: sessions, the WebSocket
  client table and every socket write. Other tasks reach it through
  `httpd_queue_work()` (lock toggle, closing clients, terminal output).
- **usb_rx / usb_tx tasks** move bytes between USB and `terminal_bridge`.
  Subscribers such as the web server run in usb_rx and must not block; the
  web server only filters and queues output there.
- **lvgl_ui task** is the only code that touches LVGL. Other tasks post to a
  mailbox (`lvgl_ui_set_credentials`, `lvgl_ui_notify_*`).
- **buttons task** polls the board every 50 ms and runs the callbacks.
- **ap_guard task** checks AP exposure every 10 s.

The USB tasks and the buttons task are registered with the task watchdog.

## Boot

`app_main` never aborts. In order:

1. NVS (erased and retried if its layout is unusable; storage is skipped if it still fails)
2. terminal bridge, board
3. credentials and TLS identity, with `bootloader_random_enable()` because WiFi is not up yet
4. display, buttons, USB console (the bridge works even if nothing else does)
5. AP, web server, mDNS, idle guard; skipped if a new password could not be shown
6. the running OTA image is confirmed only if the bridge, AP and web server all came up

## Boards

A board is one C file implementing `board.h` (buttons, battery, power off)
and, if it has a screen, `board_display.h`. Pick it with
`CONFIG_SERIALITE_BOARD_*` (`idf.py menuconfig` -> Serialite board).

| Board | Display | Notes |
|---|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-1.8 | SH8601 368x448 | default; TCA9554 expander, AXP2101 PMIC |
| Generic ESP32-S3 (headless) | none | one button on a GPIO; see below |

To add a board: copy the closest file in `components/board/src`, add a
choice to `components/board/Kconfig` and the source to its `CMakeLists.txt`,
and list the driver components it needs in `idf_component.yml`. The screen
layout in `lvgl_ui.c` is tuned for 448x368 landscape; other panel sizes need
their own layout constants.

Without a display, a freshly generated WiFi password cannot be shown, so the
AP only starts with credentials stored in encrypted NVS, or with
`CONFIG_SERIALITE_UART_SHOW_PASSWORD` printing them on the debug UART.

Other chips: the ESP32-C3/C6 also have USB Serial/JTAG and should only need
a board file and a different target. The ESP32 and ESP32-S2 lack it and need
a UART or TinyUSB implementation of `usb_console.h`.

## Using it as part of a larger firmware

Serialite is a single app. To run it next to other apps (for example as an
app in an ESP32 "OS" such as Tactility), the pieces that would move are
`terminal_bridge`, `usb_console`, `web_server`, `credentials` and
`app_credentials`; the board, buttons and screen would come from the host
firmware. The callbacks in `main/app_main.c` are the seam to replace.
