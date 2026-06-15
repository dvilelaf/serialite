#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HARNESS_DIR="$ROOT_DIR/tools/esp_http_harness"
PORT="${ESP32_KVM_HTTP_HARNESS_PORT:-8080}"
PASSWORD="${ESP32_KVM_HTTP_HARNESS_PASSWORD:-alpha zoom}"
ITERATIONS="${ESP32_KVM_HTTP_HARNESS_ITERATIONS:-20}"

cd "$HARNESS_DIR"

if ! command -v idf.py >/dev/null 2>&1; then
    if [ -f /home/david/esp-idf/export.sh ]; then
        # shellcheck disable=SC1091
        source /home/david/esp-idf/export.sh >/tmp/esp-idf-export.log 2>&1
    fi
fi

idf.py --preview build >/tmp/esp32-kvm-http-harness-build.log

log_file="$(mktemp /tmp/esp32-kvm-http-harness.XXXXXX.log)"
cookie_file="$(mktemp /tmp/esp32-kvm-http-harness.XXXXXX.cookies)"
compact_cookie_file="$(mktemp /tmp/esp32-kvm-http-harness.XXXXXX.compact.cookies)"
pid=""

cleanup() {
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$cookie_file" "$compact_cookie_file"
}
trap cleanup EXIT

./build/esp32_kvm_http_harness.elf >"$log_file" 2>&1 &
pid=$!

for _ in $(seq 1 100); do
    if curl -fsS --max-time 1 -o /dev/null "http://127.0.0.1:${PORT}/login" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "harness exited before becoming ready" >&2
        cat "$log_file" >&2
        exit 1
    fi
    sleep 0.1
done

if [ "${ESP32_KVM_SKIP_BROWSER_TEST:-0}" != "1" ]; then
    if command -v npm >/dev/null 2>&1; then
        if [ ! -d node_modules/@playwright/test ]; then
            npm install
        fi
        ESP32_KVM_HTTP_HARNESS_BASE_URL="http://127.0.0.1:${PORT}" npm run test:browser
        sleep 1.1
    else
        echo "npm not found; skipping browser functional test" >&2
    fi
fi

curl_status() {
    local path="$1"
    shift
    curl -sS --max-time 2 -D - -o /dev/null "$@" "http://127.0.0.1:${PORT}${path}" | sed -n '1s/\r$//p'
}

expect_status() {
    local name="$1"
    local expected="$2"
    shift 2
    local actual
    actual="$(curl_status "$@")"
    if [ "$actual" != "$expected" ]; then
        echo "${name}: expected '${expected}', got '${actual}'" >&2
        cat "$log_file" >&2
        exit 1
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "${name}: harness exited unexpectedly" >&2
        cat "$log_file" >&2
        exit 1
    fi
}

expect_status_or_rate_limit() {
    local name="$1"
    local expected="$2"
    shift 2
    local actual
    actual="$(curl_status "$@")"
    case "$actual" in
        "$expected"|"HTTP/1.1 429 Too Many Requests") ;;
        *)
            echo "${name}: expected '${expected}' or rate limit, got '${actual}'" >&2
            cat "$log_file" >&2
            exit 1
            ;;
    esac
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "${name}: harness exited unexpectedly" >&2
        cat "$log_file" >&2
        exit 1
    fi
}

for i in $(seq 1 "$ITERATIONS"); do
    if [ "$i" = 1 ]; then
        expect_status "root redirect" "HTTP/1.1 303 See Other" "/"
        expect_status "favicon 404" "HTTP/1.1 404 Not Found" "/favicon.ico"
        expect_status "login page" "HTTP/1.1 200 OK" "/login"
        expect_status "login post" "HTTP/1.1 303 See Other" "/login" -c "$cookie_file" -X POST
        expect_status "second login post" "HTTP/1.1 303 See Other" "/login" -c "$compact_cookie_file" -X POST
    else
        expect_status_or_rate_limit "root redirect" "HTTP/1.1 303 See Other" "/"
        expect_status_or_rate_limit "favicon 404" "HTTP/1.1 404 Not Found" "/favicon.ico"
        expect_status_or_rate_limit "login page" "HTTP/1.1 200 OK" "/login"
        expect_status_or_rate_limit "login post" "HTTP/1.1 303 See Other" "/login" -c "$cookie_file" -X POST
    fi

    root_status="$(curl_status "/" -b "$cookie_file")"
    terminal_status="$(curl_status "/terminal" -b "$cookie_file")"
    terminal_json_status="$(curl_status "/terminal-status.json" -b "$cookie_file")"

    case "$root_status" in
        "HTTP/1.1 200 OK"|"HTTP/1.1 429 Too Many Requests") ;;
        *)
            echo "auth root: unexpected '${root_status}'" >&2
            cat "$log_file" >&2
            exit 1
            ;;
    esac
    if [ "$i" = 1 ]; then
        compact_root_status="$(curl_status "/" -b "$compact_cookie_file")"
        case "$compact_root_status" in
            "HTTP/1.1 200 OK") ;;
            *)
                echo "compact auth root: unexpected '${compact_root_status}'" >&2
                cat "$log_file" >&2
                exit 1
                ;;
        esac
    fi
    case "$terminal_status" in
        "HTTP/1.1 200 OK"|"HTTP/1.1 429 Too Many Requests") ;;
        *)
            echo "terminal: unexpected '${terminal_status}'" >&2
            cat "$log_file" >&2
            exit 1
            ;;
    esac
    case "$terminal_json_status" in
        "HTTP/1.1 200 OK"|"HTTP/1.1 429 Too Many Requests") ;;
        *)
            echo "terminal status: unexpected '${terminal_json_status}'" >&2
            cat "$log_file" >&2
            exit 1
            ;;
    esac

    kill -0 "$pid"
    if [ $((i % 5)) -eq 0 ]; then
        echo "harness iteration ${i}/${ITERATIONS}"
    fi
    sleep 0.25
done

echo "http harness ok (${ITERATIONS} iterations)"
