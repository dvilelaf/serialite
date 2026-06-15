# Development

## Prerequisites

Install ESP-IDF 5.x and source its environment:

```bash
source /path/to/esp-idf/export.sh
```

## Verify

```bash
./scripts/dev-verify.sh
```

## Build And Flash

```bash
idf.py set-target esp32s3
idf.py build
ls /dev/serial/by-id/*Espressif*
idf.py -p /dev/serial/by-id/<the-esp32-path> flash
```

If flashing fails because of USB permissions or serial noise, reconnect the board and use a lower baud rate:

```bash
idf.py -p /dev/serial/by-id/<the-esp32-path> -b 115200 flash
```

## Release Bundle

```bash
./scripts/package-release.sh --version vX.Y.Z
```

Publish the generated `dist/esp32-kvm-vX.Y.Z.tar.gz` as a release asset. Do not commit `dist/` or ESP-IDF build outputs.
