# Third-party notices

Serialite ships or links the following third-party code. Firmware images
include the libraries listed under "Linked into the firmware".

## Vendored in this repository

### xterm.js and @xterm/addon-fit

- Files: `components/web_server/assets/xterm.js`, `components/web_server/assets/xterm.css`
- Version: `@xterm/xterm` 6.0.0 and `@xterm/addon-fit` 0.11.0, bundled into one
  IIFE with `esbuild --bundle --minify --format=iife --target=es2018`. The
  bundle exposes `window.SerialiteXterm`.
- License: MIT. Copyright (c) 2014-2024 The xterm.js authors; Copyright (c)
  2012-2013 Christopher Jeffrey; originally forked from Fabrice Bellard's
  jslinux vt100. The notices are kept at the end of `xterm.js`.
- SHA-256: `xterm.js` 7512f4926d7a8017cd08e5f8bd053ad11a188725b9f232377e4798bb21807d05,
  `xterm.css` 854a7c0fb70e8b1a083c16797ab827299fb18744f5ad34f227b48337e33293c6

To rebuild: install `@xterm/xterm@6.0.0` and `@xterm/addon-fit@0.11.0`, bundle an
entry file that assigns `window.SerialiteXterm = {Terminal, FitAddon}` with
the esbuild command above, and update the hashes.

### EFF Large Wordlist

- File: `components/credentials/wordlist/eff_large_wordlist.txt`, compiled into
  `components/credentials/src/credentials_wordlist.c` by `scripts/gen_wordlist.py`
- Source: https://www.eff.org/dice (SHA-256
  addd35536511597a02fa0a9ff1e5284677b8883b83e986e43f15a3db996b903e)
- License: Creative Commons Attribution 3.0 United States (CC BY 3.0 US),
  Electronic Frontier Foundation.

## Linked into the firmware (fetched at build time)

| Component | Version | License |
|---|---|---|
| ESP-IDF | 5.3.2 | Apache-2.0 |
| LVGL (`lvgl/lvgl`) | 9.5.0 | MIT |
| `espressif/mdns` | 1.11.1 | Apache-2.0 |
| `espressif/esp_lcd_sh8601` | 1.0.0 | Apache-2.0 |
| `espressif/esp_io_expander_tca9554` | 1.0.1 | Apache-2.0 |
| `espressif/esp_io_expander` | 1.2.1 | Apache-2.0 |
| mbedTLS (part of ESP-IDF) | 3.x | Apache-2.0 |

Exact versions are pinned in `dependencies.lock`.
