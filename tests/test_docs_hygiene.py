#!/usr/bin/env python3
"""Documentation hygiene checks for public onboarding paths."""

from __future__ import annotations

import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class DocsHygieneTest(unittest.TestCase):
    def test_public_docs_do_not_use_machine_local_paths(self) -> None:
        public_docs = [
            REPO_ROOT / "README.md",
            REPO_ROOT / "docs" / "development.md",
            REPO_ROOT / "tools" / "esp_http_harness" / "run_http_harness_tests.sh",
        ]

        for path in public_docs:
            with self.subTest(path=path.relative_to(REPO_ROOT)):
                self.assertNotIn("/home/david", path.read_text())

    def test_readme_is_a_short_main_branch_quickstart(self) -> None:
        readme = (REPO_ROOT / "README.md").read_text()

        self.assertIn("https://raw.githubusercontent.com/dvilelaf/serialite/main/tools/host/setup-linux-serial-console.sh", readme)
        self.assertIn("docs/development.md", readme)
        self.assertNotIn("vX.Y.Z", readme)
        self.assertNotIn("Firmware Releases", readme)
        self.assertNotIn("Host Setup", readme)
        self.assertNotIn("scripts/package-release.sh", readme)
        self.assertNotIn("scripts/verify.sh", readme)
        self.assertNotIn("verify_production_profile.py", readme)


if __name__ == "__main__":
    unittest.main()
