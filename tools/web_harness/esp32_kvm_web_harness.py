#!/usr/bin/env python3
"""Local development harness for the ESP32-KVM Web UI.

This is not production firmware code. It runs on the workstation so the Web UI
contract can be exercised without joining the ESP32 access point.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.server
import json
import secrets
import socketserver
import struct
import urllib.parse
from dataclasses import dataclass, field
from http import HTTPStatus


SESSION_COOKIE = "kvm_session"


@dataclass
class HarnessState:
    usb_connected: bool = True
    sessions: dict[str, str] = field(default_factory=dict)
    locked: bool = False

    def create_session(self) -> tuple[str, str]:
        token = secrets.token_hex(16)
        csrf = secrets.token_hex(16)
        self.sessions.clear()
        self.sessions[token] = csrf
        self.locked = False
        return token, csrf

    def csrf_for(self, token: str | None) -> str | None:
        if token is None or self.locked:
            return None
        return self.sessions.get(token)

    def invalidate_all(self) -> None:
        self.sessions.clear()
        self.locked = True


class ReusableThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class KvmHarnessHandler(http.server.BaseHTTPRequestHandler):
    server_version = "ESP32KVMHarness/1.0"

    @property
    def state(self) -> HarnessState:
        return self.server.state  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: object) -> None:
        if getattr(self.server, "quiet", False):  # type: ignore[attr-defined]
            return
        super().log_message(fmt, *args)

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/":
            self.redirect("/terminal" if self.session_token() else "/login")
        elif self.path == "/login":
            self.send_html(login_page())
        elif self.path == "/terminal":
            if not self.require_session_redirect():
                return
            self.send_html(terminal_page(self.csrf_token() or "", self.state.usb_connected))
        elif self.path == "/terminal-status.json":
            if not self.require_session_json():
                return
            self.send_json(
                {
                    "usb_connected": self.state.usb_connected,
                    "demo_active": False,
                    "demo_bytes": 0,
                    "writer_state": self.writer_state(),
                }
            )
        elif self.path == "/diagnostics":
            self.send_html("<!doctype html><title>Diagnostics</title><pre>Harness diagnostics</pre>")
        elif self.path == "/ws":
            self.handle_websocket()
        else:
            self.send_error(HTTPStatus.NOT_FOUND, "not found")

    def do_POST(self) -> None:  # noqa: N802
        if self.path == "/login":
            token, _csrf = self.state.create_session()
            self.send_response(HTTPStatus.SEE_OTHER)
            self.no_store_headers()
            self.send_header("Location", "/terminal")
            self.send_header("Set-Cookie", f"{SESSION_COOKIE}={token}; HttpOnly; SameSite=Strict; Path=/")
            self.end_headers()
        elif self.path == "/logout":
            if not self.require_csrf():
                return
            token = self.session_token()
            if token is not None:
                self.state.sessions.pop(token, None)
            self.send_response(HTTPStatus.SEE_OTHER)
            self.no_store_headers()
            self.send_header("Location", "/login")
            self.send_header("Set-Cookie", f"{SESSION_COOKIE}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0")
            self.end_headers()
        elif self.path == "/api/emergency-lock":
            if not self.require_csrf():
                return
            self.state.invalidate_all()
            self.send_text("locking")
        else:
            self.send_error(HTTPStatus.NOT_FOUND, "not found")

    def read_form(self) -> dict[str, list[str]]:
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8", errors="replace")
        return urllib.parse.parse_qs(body, keep_blank_values=True)

    def session_token(self) -> str | None:
        cookies = self.headers.get("Cookie", "")
        for part in cookies.split(";"):
            name, sep, value = part.strip().partition("=")
            if sep and name == SESSION_COOKIE:
                return value
        return None

    def csrf_token(self) -> str | None:
        return self.state.csrf_for(self.session_token())

    def writer_state(self) -> str:
        token = self.session_token()
        if self.state.locked:
            return "locked"
        if token is None or token not in self.state.sessions:
            return "invalid-session"
        return "write-active"

    def require_session_redirect(self) -> bool:
        if self.csrf_token() is not None:
            return True
        self.redirect("/login")
        return False

    def require_session_json(self) -> bool:
        if self.csrf_token() is not None:
            return True
        self.send_text("auth required", HTTPStatus.UNAUTHORIZED)
        return False

    def require_csrf(self) -> bool:
        expected = self.csrf_token()
        supplied = self.headers.get("X-CSRF-Token")
        if expected is None:
            self.send_text("auth required", HTTPStatus.UNAUTHORIZED)
            return False
        if supplied != expected:
            self.send_text("invalid csrf", HTTPStatus.FORBIDDEN)
            return False
        return True

    def no_store_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Frame-Options", "DENY")

    def redirect(self, location: str) -> None:
        self.send_response(HTTPStatus.SEE_OTHER)
        self.no_store_headers()
        self.send_header("Location", location)
        self.end_headers()

    def send_html(self, body: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        encoded = body.encode("utf-8")
        self.send_response(status)
        self.no_store_headers()
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def send_json(self, body: dict[str, object]) -> None:
        encoded = json.dumps(body, separators=(",", ":")).encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.no_store_headers()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def send_text(self, body: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        encoded = body.encode("utf-8")
        self.send_response(status)
        self.no_store_headers()
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def handle_websocket(self) -> None:
        key = self.headers.get("Sec-WebSocket-Key")
        if self.headers.get("Upgrade", "").lower() != "websocket" or key is None:
            self.send_error(HTTPStatus.BAD_REQUEST, "websocket upgrade required")
            return
        accept = base64.b64encode(
            hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
        ).decode("ascii")
        self.send_response(HTTPStatus.SWITCHING_PROTOCOLS)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        payload = b"ESP32-KVM harness serial stream ready.\r\nlogin: "
        self.wfile.write(struct.pack("!BB", 0x81, len(payload)) + payload)


def login_page() -> str:
    return """<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>KVM Login</title><style>
*{box-sizing:border-box}body{margin:0;min-height:100svh;background:linear-gradient(180deg,#020504,#07130f);color:#eafff8;font:16px sans-serif;display:grid;place-items:center;padding:18px}
main{width:100%;max-width:380px;border:1px solid #174436;border-radius:22px;padding:22px;background:#030807;box-shadow:0 18px 50px #0009}
h1{margin:0 0 8px;font-size:24px}button{width:100%;border-radius:12px;padding:13px;margin-top:12px;font:inherit;background:#0c3429;color:#bffff0;border:1px solid #2ee6b8;font-weight:700}
p{color:#8bb5aa;line-height:1.4;margin:8px 0 14px}</style></head><body><main><h1>Serial console</h1>
<p>This opens a local web session on the KVM access point. Authenticate in the Linux terminal when prompted.</p>
<form method="post" action="/login"><button type="submit">Open console</button></form></main></body></html>"""


def terminal_page(csrf: str, usb_connected: bool) -> str:
    usb = "true" if usb_connected else "false"
    return f"""<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Serial console</title><style>
:root{{--line:#18352d;--hot:#8fffe4;--bad:#ff8b68;--text:#eafff8;--muted:#78958d}}
*{{box-sizing:border-box}}html,body{{margin:0;height:100%;background:#030504;color:var(--text);font:14px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;overflow:hidden}}
#hud{{position:fixed;right:12px;top:12px;z-index:5;display:flex;align-items:center;gap:8px;padding:7px;border:1px solid #1f4b3f;border-radius:999px;background:rgba(4,10,8,.82);box-shadow:0 14px 40px #000b}}
.pill{{border:1px solid var(--line);border-radius:999px;padding:5px 9px;color:var(--muted);background:#050907;font-size:12px}}button,a{{font:inherit;color:var(--text)}}button{{border:1px solid var(--line);border-radius:10px;background:#0a1813;padding:6px 10px;font-weight:700}}button.danger{{border-color:var(--bad);color:#ffd4c7}}
#keys{{display:none;position:fixed;right:12px;top:62px;z-index:4;width:min(310px,calc(100vw - 24px));padding:8px;border:1px solid var(--line);border-radius:14px;background:#06100d;box-shadow:0 20px 50px #000b}}
#keys.open{{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}}#keys a{{grid-column:1/-1;text-align:center;color:var(--hot);text-decoration:none;padding:7px}}
#terminal{{position:fixed;inset:0;outline:none;overflow:auto;white-space:pre-wrap;word-break:break-word;padding:18px 18px 70px;background:radial-gradient(circle at 70% -30%,#112b23 0,#030504 42%);line-height:1.38}}
#kbd{{position:fixed;inset:0;opacity:.01;color:transparent;background:transparent;border:0;resize:none}}
</style></head><body><div id="hud"><span id="state" class="pill">Stream OK</span><div id="menu"><button id="menuBtn">More</button><div id="keys">
<button data-k="\\u0003">Ctrl+C</button><button data-k="\\u0004">Ctrl+D</button><button data-k="\\r">Enter</button><button data-k="\\u001b">Esc</button><button data-k="\\t">Tab</button>
<button class="danger" id="panic">Emergency lock</button><button class="danger" id="logout">Sign out</button><a href="/diagnostics">Diagnostics</a></div></div></div>
<div id="terminal" tabindex="0">Control active. Press Enter to wake console.</div><textarea id="kbd"></textarea>
<script>const CSRF='{csrf}';let canWrite=true,usbConnected={usb},writerState='write-active',locked=false,connected=true;</script></body></html>"""


def create_server(address: tuple[str, int], quiet: bool = True) -> ReusableThreadingHTTPServer:
    server = ReusableThreadingHTTPServer(address, KvmHarnessHandler)
    server.state = HarnessState()  # type: ignore[attr-defined]
    server.quiet = quiet  # type: ignore[attr-defined]
    return server


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the ESP32-KVM local Web UI harness.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    server = create_server((args.host, args.port), quiet=not args.verbose)
    host, port = server.server_address
    print(f"ESP32-KVM Web harness: http://{host}:{port}/login")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
