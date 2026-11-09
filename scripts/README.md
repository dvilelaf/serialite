# Scripts

For maintainers. End users only need the root `README.md`.

- `verify`: every check, locally (`changed` or `full`); see `docs/development.md`.
- `lint_production_profile.py`: checks the production profile and both partition tables.
- `gen_wordlist.py`: regenerates `credentials_wordlist.c` from the EFF list (`--check` verifies it).
- `package-release.sh`: builds the release assets under `dist/`.
