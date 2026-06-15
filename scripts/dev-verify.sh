#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

cmake -S tests/host -B build-host-tests -G Ninja
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
python3 scripts/lint_production_profile.py

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py not found. Source your ESP-IDF export.sh first." >&2
    exit 127
fi

if ! grep -q '^CONFIG_IDF_TARGET="esp32s3"$' sdkconfig 2>/dev/null; then
    idf.py set-target esp32s3
fi

idf.py build
