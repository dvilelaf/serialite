# Scripts

For maintainers and CI. End users only need the root `README.md`.

- `dev-verify.sh`: everything CI runs, locally.
- `lint_production_profile.py`: checks the production profile and both partition tables.
- `gen_wordlist.py`: regenerates `credentials_wordlist.c` from the EFF list (`--check` in CI).
- `package-release.sh`: builds the release assets under `dist/`.
