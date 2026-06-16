# Scripts

These scripts are for maintainers and CI. End users should follow the root `README.md`.

- `dev-verify.sh`: full local verification: host tests, Python tests, production-profile lint, and ESP-IDF build.
- `lint_production_profile.py`: static policy lint for production security defaults and partition layout.
- `package-release.sh`: creates a `dist/` release bundle with firmware binaries, host setup script, manifest, and SHA256 checksums.
- `../tools/flash-latest-firmware.sh`: downloads/caches a release firmware bundle and flashes it to the ESP32-S3.
