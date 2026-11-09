#!/usr/bin/env python3
"""HTTP and WebSocket security checks against a running Linux web harness.

Usage: security_checks.py <port> <harness-log>

Each check speaks raw HTTP so it can send headers a browser never would
(foreign Host, missing Origin). Handlers in the harness print "HARNESS <name>"
when reached; a rejected request must leave no such marker behind.
"""

from __future__ import annotations

import base64
import http.client
import json
import os
import pathlib
import re
import socket
import sys
import time

PORT = int(sys.argv[1])
LOG = pathlib.Path(sys.argv[2])
HOST = f"127.0.0.1:{PORT}"
ORIGIN = f"http://{HOST}"
failures: list[str] = []


def check(name: str, condition: bool, detail: str = "") -> None:
    print(f"{'ok  ' if condition else 'FAIL'} {name}{': ' + detail if detail and not condition else ''}")
    if not condition:
        failures.append(name)


def request(method: str, path: str, headers: dict[str, str] | None = None, body: bytes | None = None):
    conn = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    conn.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
    headers = {"Host": HOST, **(headers or {})}
    if body is not None:
        headers.setdefault("Content-Length", str(len(body)))
    for key, value in headers.items():
        conn.putheader(key, value)
    conn.endheaders()
    if body is not None:
        try:
            conn.send(body)
        except OSError:
            pass  # the server may close early on a rejected upload
    try:
        response = conn.getresponse()
        data = response.read()
        return response.status, response.headers, data
    except (http.client.HTTPException, OSError) as exc:
        return 0, {}, str(exc).encode()
    finally:
        conn.close()


def marker_count(name: str) -> int:
    return LOG.read_text(errors="replace").count(f"HARNESS {name}")


def new_session() -> tuple[str, str]:
    status, headers, body = request("POST", "/api/session", {"Origin": ORIGIN})
    if status != 200:
        raise SystemExit(f"cannot open a session: {status} {body!r}")
    cookie = re.search(r"serialite_session=([0-9a-f]{32})", headers.get("Set-Cookie", "")).group(1)
    return cookie, json.loads(body)["csrf"]


def authed(cookie: str, csrf: str | None = None, origin: str | None = ORIGIN) -> dict[str, str]:
    headers = {"Cookie": f"serialite_session={cookie}"}
    if csrf is not None:
        headers["X-CSRF-Token"] = csrf
    if origin is not None:
        headers["Origin"] = origin
    return headers


def websocket_first_frame(headers: dict[str, str]) -> bytes | None:
    """Opens /ws and returns the first frame payload, or None if the server hangs up."""
    sock = socket.create_connection(("127.0.0.1", PORT), timeout=3)
    key = base64.b64encode(os.urandom(16)).decode()
    lines = [
        "GET /ws HTTP/1.1",
        f"Host: {HOST}",
        "Upgrade: websocket",
        "Connection: Upgrade",
        f"Sec-WebSocket-Key: {key}",
        "Sec-WebSocket-Version: 13",
    ] + [f"{k}: {v}" for k, v in headers.items()]
    sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
    data = b""
    try:
        while b"\r\n\r\n" not in data:
            chunk = sock.recv(4096)
            if not chunk:
                return None
            data += chunk
        rest = data.split(b"\r\n\r\n", 1)[1]
        while len(rest) < 2:
            chunk = sock.recv(4096)
            if not chunk:
                return None
            rest += chunk
        length = rest[1] & 0x7F
        while len(rest) < 2 + length:
            chunk = sock.recv(4096)
            if not chunk:
                return None
            rest += chunk
        return rest[2 : 2 + length]
    except (socket.timeout, ConnectionResetError):
        return None
    finally:
        sock.close()


# ---- static pages and headers ------------------------------------------------

status, headers, _ = request("GET", "/")
check("root redirects to the terminal", status == 303 and headers.get("Location") == "/terminal")
status, headers, body = request("GET", "/terminal")
csp = headers.get("Content-Security-Policy", "")
check("terminal page is public static HTML", status == 200 and b"<div id=\"terminal\"" in body)
check("terminal page carries no secrets", b"csrf" not in body.lower() and "Set-Cookie" not in headers)
check("CSP forbids inline scripts", "script-src 'self';" in csp and "frame-ancestors 'none'" in csp, csp)
check("pages refuse framing", headers.get("X-Frame-Options") == "DENY")
status, _, _ = request("GET", "/nope")
check("unknown path is 404", status == 404)
status, _, _ = request("POST", "/terminal", body=b"")
check("wrong method is 405", status == 405)

# ---- Host allowlist (DNS rebinding) -------------------------------------------

status, _, _ = request("GET", "/terminal", {"Host": f"evil.example:{PORT}"})
check("foreign Host is refused", status == 421)
status, _, _ = request("POST", "/api/session", {"Host": f"evil.example:{PORT}", "Origin": f"http://evil.example:{PORT}"})
check("rebound origin cannot open a session", status == 421)

# ---- session creation ------------------------------------------------------------

status, _, _ = request("POST", "/api/session")
check("session needs an Origin header", status == 403)
status, _, _ = request("POST", "/api/session", {"Origin": "http://evil.example"})
check("session refuses foreign Origin", status == 403)
status, headers, body = request("POST", "/api/session", {"Origin": ORIGIN})
cookie_header = headers.get("Set-Cookie", "")
check("same-origin POST opens a session", status == 200 and "HttpOnly" in cookie_header and "SameSite=Strict" in cookie_header)
check("session response carries a CSRF token", status == 200 and re.fullmatch(r"[0-9a-f]{32}", json.loads(body)["csrf"]) is not None)

cookie, csrf = new_session()
status, _, _ = request("GET", "/api/status")
check("API needs a session", status == 401)
status, _, body = request("GET", "/api/status", authed(cookie, origin=None))
check("API answers with a session", status == 200 and json.loads(body)["locked"] is False)

# ---- the fail-open regression: rejected requests never reach handlers -------------

upload = bytes([0xE9]) + bytes(4095)
before = marker_count("ota_update_begin")
status, _, _ = request("POST", "/api/ota", {"Origin": "http://evil.example", "Content-Type": "application/octet-stream"}, upload)
check("OTA without session or CSRF is refused", status in (401, 403))
status, _, _ = request("POST", "/api/ota", authed(cookie), upload)
check("OTA without CSRF token is refused", status == 403)
status, _, _ = request("POST", "/api/ota", authed(cookie, "0" * 32), upload)
check("OTA with a wrong CSRF token is refused", status == 403)
status, _, _ = request("POST", "/api/ota", authed(cookie, csrf, origin="http://evil.example"), upload)
check("OTA from a foreign Origin is refused", status == 403)
status, _, _ = request("POST", "/api/ota", {**authed(cookie, csrf), "Host": f"evil.example:{PORT}"}, upload)
check("OTA through a foreign Host is refused", status == 421)
check("no rejected OTA reached the updater", marker_count("ota_update_begin") == before)

status, _, _ = request("POST", "/api/ota", authed(cookie, csrf), upload)
check("authorised OTA reaches the updater", marker_count("ota_update_begin") == before + 1, f"status {status}")

before = marker_count("import_config")
status, _, _ = request("POST", "/api/config", authed(cookie, csrf), b"{" + b" " * 600 + b"}")
check("oversized config body is refused", status == 413)
check("oversized body never reached the importer", marker_count("import_config") == before)
status, _, _ = request("POST", "/api/config", authed(cookie, csrf), b'{"schema":"harness"}')
check("valid config import is accepted", status == 200 and marker_count("import_config") == before + 1)

before = marker_count("rotate_credentials")
status, _, _ = request("POST", "/api/credentials/rotate", authed(cookie))
check("rotation without CSRF never reaches the callback", status == 403 and marker_count("rotate_credentials") == before)

# ---- WebSocket ----------------------------------------------------------------------

frame = websocket_first_frame({"Cookie": f"serialite_session={cookie}", "Origin": ORIGIN})
check("terminal WebSocket streams scrollback", frame is not None and b"harness login" in frame, repr(frame))
check("WebSocket from a foreign Origin is dropped",
      websocket_first_frame({"Cookie": f"serialite_session={cookie}", "Origin": "http://evil.example"}) is None)
check("WebSocket without a session is dropped", websocket_first_frame({"Origin": ORIGIN}) is None)

# ---- one operator at a time ---------------------------------------------------------

other_cookie, other_csrf = new_session()
status, _, _ = request("GET", "/api/status", authed(cookie, origin=None))
check("a new session replaces the previous one", status == 401)
cookie, csrf = other_cookie, other_csrf

# ---- rate limit -----------------------------------------------------------------------

statuses = [request("GET", "/assets/common.js")[0] for _ in range(60)]
check("bursts are rate limited", 429 in statuses and 200 in statuses)
time.sleep(1.2)

# ---- emergency lock (last: only the device button can release it) -------------------

status, _, _ = request("POST", "/api/emergency-lock", authed(cookie, csrf))
check("emergency lock engages", status == 200)
status, _, _ = request("GET", "/api/status", authed(cookie, origin=None))
check("emergency lock ends the session", status == 401)
status, _, _ = request("POST", "/api/session", {"Origin": ORIGIN})
check("emergency lock refuses new sessions", status == 423)

if failures:
    print(f"{len(failures)} security check(s) failed", file=sys.stderr)
    sys.exit(1)
print("all security checks passed")
