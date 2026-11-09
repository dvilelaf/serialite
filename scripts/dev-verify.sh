#!/usr/bin/env bash
# Everything CI runs, locally. Needs cmake, ninja and python3; the firmware
# build and the web harness also need ESP-IDF sourced, and the browser tests npm.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

step() {
    printf '\n== %s\n' "$*"
}

step "host unit tests"
cmake -S tests/host -B build-host-tests -G Ninja -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure

step "script tests"
python3 -m unittest discover -s tests -p 'test_*.py'

step "generated word list is current"
python3 scripts/gen_wordlist.py --check

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py not found: skipping the production lint, firmware builds and web harness." >&2
    echo "Source ESP-IDF's export.sh to run them." >&2
    exit 0
fi

step "production profile"
scripts/lint_production_profile.py

step "firmware build"
idf.py build

step "web harness"
tools/esp_http_harness/run_http_harness_tests.sh
