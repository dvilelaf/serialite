# Development

## Setup

ESP-IDF 5.3.x, plus `cmake`, `ninja`, `gcc`, `python3`, `uv`, `npm`, `flock` and
`gitleaks` for the checks.

```bash
. /path/to/esp-idf/export.sh
git config core.hooksPath .githooks   # once per clone: every commit runs the checks
```

## Build and flash

```bash
idf.py build
idf.py -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<id>-if00 flash
```

Firmware logs go to UART0, not USB: the USB port belongs to the server's
console. Use a USB-UART adapter on the board's UART pins to read them.

Other configurations, each in its own build directory:

```bash
# Generic ESP32-S3 devkit without a display
printf 'CONFIG_SERIALITE_BOARD_GENERIC_HEADLESS=y\n' > /tmp/headless.defaults
idf.py -B build-headless -D SDKCONFIG=build-headless/sdkconfig \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;/tmp/headless.defaults" build

# Production profile (needs the signing key named in sdkconfig.prod.defaults)
idf.py -B build-prod -D SDKCONFIG=build-prod/sdkconfig \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.prod.defaults" build
```

## Checks

There is no cloud CI: every check runs on your machine through one script.

```bash
scripts/verify changed           # what this branch changed; the pre-commit hook runs it on staged files
scripts/verify full              # everything; run it before a release
```

`changed` only runs the checks the changed files can affect. `full` runs:

- gitleaks over the whole history (false positives go in `.gitleaksignore`).
- ruff (format and lint) on the Python scripts and tests.
- `tests/*.py`: the flasher, packaging, host setup script, docs and production lint.
- `scripts/gen_wordlist.py --check`: the generated word list matches the EFF file.
- shellcheck on every shell script.
- `tests/host`: unit tests for every hardware-independent module, with
  `-Werror`, ASan and UBSan. Add a test with one `serialite_test()` line in
  `tests/host/CMakeLists.txt`.
- `scripts/lint_production_profile.py`.
- Firmware builds of the lab, headless and production profiles (production
  signed with a throwaway key) in `build-verify-*`. Any warning in Serialite's
  own components is an error (`tools/cmake/strict_warnings.cmake`).
- `tools/esp_http_harness/run_http_harness_tests.sh`: builds the real
  `web_server` for the ESP-IDF linux target with stub hardware, runs the
  Playwright browser flows, then `security_checks.py` (raw HTTP and WebSocket
  requests a browser would never send).
- jscpd: fails on new copy-pasted code.
- semgrep `p/default` (exceptions are marked `nosemgrep` with a reason).
- diff coverage: at least 80 % of the changed lines that host or script tests can reach.

Tool versions are pinned at the top of `scripts/verify`. When `idf.py` is not
on PATH it sources ESP-IDF itself, from `$IDF_EXPORT`, the script set with
`git config verify.idfExport <path>`, or `$IDF_PATH/export.sh`. A missing tool
fails its check; never bypass the hook with `--no-verify`.

## Web assets

Pages live in `components/web_server/assets` as plain HTML, CSS and JS and are
embedded with `EMBED_FILES`. Scripts must stay in files: the CSP forbids inline
scripts. A new asset needs an `EMBED_FILES` entry and a line in the route table
in `web_server.c`.

## Releasing

1. Run `scripts/verify full`, then tag the commit: `git tag vX.Y.Z`.
2. `scripts/package-release.sh` (lab profile by default; `--profile prod` for signed images).
3. Upload everything it lists from `dist/` to the GitHub release:
   `serialite-vX.Y.Z.tar.gz`, `serialite-vX.Y.Z.tar.gz.sha256`,
   `flash-latest-firmware.sh` and `setup-linux-serial-console.sh`.

The archive is reproducible for a given commit and build. The flasher refuses
it unless it matches the `.sha256` asset and its own `SHA256SUMS`.

Releases from 2026-11 onward change the default SSID from `KVM` to `Serialite`.
The lab partition layout is unchanged, so older lab boards can update over OTA.
