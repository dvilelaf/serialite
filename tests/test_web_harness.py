#!/usr/bin/env python3
"""Regression tests for the local Web UI harness."""

from __future__ import annotations

import http.client
import pathlib
import sys
import threading
import unittest
from http.cookies import SimpleCookie

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools" / "web_harness"))

import esp32_kvm_web_harness as harness  # noqa: E402


class HarnessServerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.server = harness.create_server(("127.0.0.1", 0), password="alpha zoom")
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.host, self.port = self.server.server_address

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request(self, method: str, path: str, body: str = "", headers: dict[str, str] | None = None) -> http.client.HTTPResponse:
        conn = http.client.HTTPConnection(self.host, self.port, timeout=5)
        conn.request(method, path, body=body, headers=headers or {})
        return conn.getresponse()

    def login(self) -> tuple[str, str]:
        response = self.request(
            "POST",
            "/login",
            body="password=alphazoom",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        self.assertEqual(response.status, 303)
        cookie = SimpleCookie(response.getheader("Set-Cookie"))
        session = cookie["kvm_session"].value

        terminal = self.request("GET", "/terminal", headers={"Cookie": f"kvm_session={session}"})
        html = terminal.read().decode()
        self.assertEqual(terminal.status, 200)
        csrf = html.split("const CSRF='", 1)[1].split("'", 1)[0]
        return session, csrf

    def test_login_requires_visible_web_password(self) -> None:
        bad = self.request(
            "POST",
            "/login",
            body="password=wrong",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        self.assertEqual(bad.status, 403)

        spaced = self.request(
            "POST",
            "/login",
            body="password=alpha+zoom",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        self.assertEqual(spaced.status, 403)

        session, _csrf = self.login()
        self.assertTrue(session)

    def test_terminal_requires_auth(self) -> None:
        response = self.request("GET", "/terminal")
        self.assertEqual(response.status, 303)
        self.assertEqual(response.getheader("Location"), "/login")

    def test_login_replaces_previous_session_and_enables_input(self) -> None:
        session, csrf = self.login()

        status = self.request("GET", "/terminal-status.json", headers={"Cookie": f"kvm_session={session}"})
        self.assertEqual(status.status, 200)
        self.assertIn('"writer_state":"write-active"', status.read().decode())

        second_session, _second_csrf = self.login()
        self.assertNotEqual(session, second_session)

        old_status = self.request("GET", "/terminal-status.json", headers={"Cookie": f"kvm_session={session}"})
        self.assertEqual(old_status.status, 401)

        new_status = self.request("GET", "/terminal-status.json", headers={"Cookie": f"kvm_session={second_session}"})
        self.assertEqual(new_status.status, 200)
        self.assertIn('"writer_state":"write-active"', new_status.read().decode())

    def test_emergency_lock_invalidates_session(self) -> None:
        session, csrf = self.login()
        response = self.request(
            "POST",
            "/api/emergency-lock",
            headers={"Cookie": f"kvm_session={session}", "X-CSRF-Token": csrf},
        )
        self.assertEqual(response.status, 200)

        status = self.request("GET", "/terminal-status.json", headers={"Cookie": f"kvm_session={session}"})
        self.assertEqual(status.status, 401)


if __name__ == "__main__":
    unittest.main()
