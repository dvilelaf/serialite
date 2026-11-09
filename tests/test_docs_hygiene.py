#!/usr/bin/env python3
"""Checks that public docs stay accurate and point users at verified downloads."""

from __future__ import annotations

import pathlib
import re
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PUBLIC_DOCS = [REPO_ROOT / "README.md", *sorted((REPO_ROOT / "docs").glob("*.md"))]


class DocsHygieneTest(unittest.TestCase):
    def test_no_machine_local_paths(self) -> None:
        for path in PUBLIC_DOCS:
            with self.subTest(path=path.relative_to(REPO_ROOT)):
                self.assertNotRegex(path.read_text(), r"/home/[a-z]+|/media/[a-z]+")

    def test_install_commands_use_release_assets_not_branches(self) -> None:
        readme = (REPO_ROOT / "README.md").read_text()
        self.assertIn("releases/latest/download/flash-latest-firmware.sh", readme)
        self.assertIn("releases/latest/download/setup-linux-serial-console.sh", readme)
        self.assertNotIn("ref=main", readme)
        self.assertNotIn("raw.githubusercontent.com", readme)
        self.assertNotRegex(readme, r"curl[^\n]*\| *sudo")

    def test_relative_links_resolve(self) -> None:
        for path in PUBLIC_DOCS:
            for target in re.findall(r"\]\(([^)#]+)\)", path.read_text()):
                if "://" in target:
                    continue
                with self.subTest(doc=path.name, link=target):
                    self.assertTrue((path.parent / target).exists())

    def test_old_product_name_is_gone(self) -> None:
        for path in PUBLIC_DOCS:
            with self.subTest(path=path.relative_to(REPO_ROOT)):
                text = path.read_text()
                self.assertNotIn("SSID: KVM", text)
                self.assertNotIn("kvm.local", text)
                self.assertNotIn("ESP32_KVM", text)


if __name__ == "__main__":
    unittest.main()
