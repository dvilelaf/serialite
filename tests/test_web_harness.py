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
        self.server = harness.create_server(("127.0.0.1", 0))
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

    def open_terminal(self) -> tuple[str, str, str]:
        terminal = self.request("GET", "/terminal")
        html = terminal.read().decode()
        self.assertEqual(terminal.status, 200)
        cookie = SimpleCookie(terminal.getheader("Set-Cookie"))
        session = cookie["kvm_session"].value
        csrf = html.split("const CSRF='", 1)[1].split("'", 1)[0]
        return session, csrf, html

    def test_terminal_creates_session_without_login_screen(self) -> None:
        session, _csrf, html = self.open_terminal()
        self.assertTrue(session)
        self.assertNotIn("Open console", html)
        self.assertNotIn("Control active", html)

    def test_index_goes_directly_to_terminal(self) -> None:
        response = self.request("GET", "/")
        self.assertEqual(response.status, 303)
        self.assertEqual(response.getheader("Location"), "/terminal")

    def test_opening_terminal_replaces_previous_session_and_enables_input(self) -> None:
        session, csrf, _html = self.open_terminal()

        status = self.request("GET", "/terminal-status.json", headers={"Cookie": f"kvm_session={session}"})
        self.assertEqual(status.status, 200)
        self.assertIn('"writer_state":"write-active"', status.read().decode())

        second_session, _second_csrf, _second_html = self.open_terminal()
        self.assertNotEqual(session, second_session)

        old_status = self.request("GET", "/terminal-status.json", headers={"Cookie": f"kvm_session={session}"})
        self.assertEqual(old_status.status, 401)

        new_status = self.request("GET", "/terminal-status.json", headers={"Cookie": f"kvm_session={second_session}"})
        self.assertEqual(new_status.status, 200)
        self.assertIn('"writer_state":"write-active"', new_status.read().decode())

    def test_emergency_lock_invalidates_session(self) -> None:
        session, csrf, _html = self.open_terminal()
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
