#!/usr/bin/env bash
# Builds the web server for the ESP-IDF linux target and tests it:
# Playwright browser flows first, then the HTTP/WebSocket security checks.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HARNESS_DIR="$ROOT_DIR/tools/esp_http_harness"
PORT=8080

cd "$HARNESS_DIR"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py not found. Source ESP-IDF's export.sh first." >&2
    exit 127
fi

build_log="$(mktemp /tmp/serialite-http-harness-build.XXXXXX.log)"
if ! idf.py --preview build >"$build_log" 2>&1; then
    tail -40 "$build_log" >&2
    exit 1
fi

log_file="$(mktemp /tmp/serialite-http-harness.XXXXXX.log)"
pid=""

stop_harness() {
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    pid=""
}
trap stop_harness EXIT

start_harness() {
    : >"$log_file"
    ./build/serialite_http_harness.elf >"$log_file" 2>&1 &
    pid=$!
    for _ in $(seq 1 100); do
        if curl -fsS --max-time 1 -o /dev/null "http://127.0.0.1:${PORT}/terminal" 2>/dev/null; then
            return
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "harness exited before becoming ready" >&2
            cat "$log_file" >&2
            exit 1
        fi
        sleep 0.1
    done
    echo "harness did not become ready" >&2
    exit 1
}

if [ "${SERIALITE_SKIP_BROWSER_TEST:-0}" != "1" ]; then
    if ! command -v npm >/dev/null 2>&1; then
        echo "npm not found; set SERIALITE_SKIP_BROWSER_TEST=1 to run only the security checks" >&2
        exit 127
    fi
    if [ ! -d node_modules/@playwright/test ]; then
        npm ci
    fi
    start_harness
    SERIALITE_HTTP_HARNESS_BASE_URL="http://127.0.0.1:${PORT}" npm run test:browser
    stop_harness
fi

# The emergency lock check ends in a state only a device button can clear, so it gets a fresh harness.
start_harness
python3 "$HARNESS_DIR/security_checks.py" "$PORT" "$log_file"
