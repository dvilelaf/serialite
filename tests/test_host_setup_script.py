#!/usr/bin/env python3
"""Regression tests for the Linux host serial-console setup script."""

from __future__ import annotations

import os
import pathlib
import subprocess
import tempfile
import textwrap
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "tools" / "host" / "setup-linux-serial-console.sh"


class HostSetupScriptTest(unittest.TestCase):
    def run_script(
        self,
        *args: str,
        env: dict[str, str] | None = None,
        stdin: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)
        return subprocess.run(
            ["sh", str(SCRIPT), *args],
            input=stdin,
            cwd=REPO_ROOT,
            env=merged_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def make_fake_host(self) -> tuple[tempfile.TemporaryDirectory[str], dict[str, str], pathlib.Path]:
        tmp = tempfile.TemporaryDirectory(prefix="esp32-kvm-host-setup-")
        root = pathlib.Path(tmp.name)
        fake_bin = root / "bin"
        dev = root / "dev"
        by_id = dev / "serial" / "by-id"
        etc = root / "etc"
        state = root / "state"
        fake_bin.mkdir()
        by_id.mkdir(parents=True)
        etc.mkdir()
        state.mkdir()

        tty = dev / "ttyACM7"
        tty.write_text("")
        (by_id / "usb-Espressif_USB_JTAG_serial_debug_unit_ABC123-if00").symlink_to(tty)

        (fake_bin / "systemctl").write_text(
            textwrap.dedent(
                f"""\
                #!/usr/bin/env sh
                echo "$@" >> "{state / "systemctl.log"}"
                exit 0
                """
            )
        )
        (fake_bin / "udevadm").write_text(
            textwrap.dedent(
                f"""\
                #!/usr/bin/env sh
                if [ "$1" = "info" ]; then
                    cat <<'EOF'
ID_VENDOR_ID=303a
ID_MODEL_ID=1001
ID_SERIAL_SHORT=ABC123
ID_MODEL=USB_JTAG_serial_debug_unit
EOF
                elif [ "$1" = "trigger" ] && [ -f "{etc / "udev" / "rules.d" / "99-esp32-kvm.rules"}" ]; then
                    ln -sfn "{tty}" "{dev / "esp32-kvm-console"}"
                fi
                exit 0
                """
            )
        )
        for command in fake_bin.iterdir():
            command.chmod(0o755)

        env = {
            "PATH": f"{fake_bin}:{os.environ['PATH']}",
            "ESP32_KVM_TEST_MODE": "1",
            "ESP32_KVM_ASSUME_ROOT": "1",
            "ESP32_KVM_DEV_DIR": str(dev),
            "ESP32_KVM_BY_ID_DIR": str(by_id),
            "ESP32_KVM_ETC_DIR": str(etc),
        }
        return tmp, env, root

    def test_installs_stable_udev_symlink_and_stable_getty_unit(self) -> None:
        tmp, env, root = self.make_fake_host()
        with tmp:
            result = self.run_script(env=env)

            self.assertEqual(result.returncode, 0, result.stderr)
            rule = root / "etc" / "udev" / "rules.d" / "99-esp32-kvm.rules"
            self.assertIn('ATTRS{idVendor}=="303a"', rule.read_text())
            self.assertIn('ATTRS{idProduct}=="1001"', rule.read_text())
            self.assertIn('ATTRS{serial}=="ABC123"', rule.read_text())
            self.assertIn('SYMLINK+="esp32-kvm-console"', rule.read_text())
            self.assertEqual((root / "dev" / "esp32-kvm-console").resolve(), root / "dev" / "ttyACM7")

            service = root / "etc" / "systemd" / "system" / "esp32-kvm-serial-console.service"
            self.assertIn("agetty", service.read_text())
            self.assertIn("/dev/esp32-kvm-console", service.read_text())

            systemctl_log = (root / "state" / "systemctl.log").read_text()
            self.assertIn("enable --now esp32-kvm-serial-console.service", systemctl_log)
            self.assertNotIn("serial-getty@ttyACM7.service", systemctl_log)

    def test_generic_ttyacm_is_not_auto_selected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="esp32-kvm-host-setup-") as tmp:
            root = pathlib.Path(tmp)
            dev = root / "dev"
            by_id = dev / "serial" / "by-id"
            dev.mkdir()
            by_id.mkdir(parents=True)
            (dev / "ttyACM0").write_text("")
            env = {
                "ESP32_KVM_TEST_MODE": "1",
                "ESP32_KVM_ASSUME_ROOT": "1",
                "ESP32_KVM_DEV_DIR": str(dev),
                "ESP32_KVM_BY_ID_DIR": str(by_id),
                "ESP32_KVM_ETC_DIR": str(root / "etc"),
            }

            result = self.run_script(env=env)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("No ESP32-KVM serial device found", result.stderr)
            self.assertIn("--device", result.stderr)

    def test_non_root_stdin_mode_fails_with_curl_sudo_instruction(self) -> None:
        result = self.run_script(env={"ESP32_KVM_ASSUME_ROOT": "0"}, stdin="")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("curl", result.stderr)
        self.assertIn("sudo sh", result.stderr)

    def test_uninstall_removes_managed_files_and_unit(self) -> None:
        tmp, env, root = self.make_fake_host()
        with tmp:
            self.assertEqual(self.run_script(env=env).returncode, 0)

            result = self.run_script("--uninstall", env=env)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((root / "etc" / "udev" / "rules.d" / "99-esp32-kvm.rules").exists())
            self.assertFalse((root / "etc" / "systemd" / "system" / "esp32-kvm-serial-console.service").exists())
            self.assertFalse((root / "dev" / "esp32-kvm-console").exists())
            systemctl_log = (root / "state" / "systemctl.log").read_text()
            self.assertIn("disable --now esp32-kvm-serial-console.service", systemctl_log)

    def test_refuses_usb_identity_that_cannot_be_written_safely_to_udev_rule(self) -> None:
        tmp, env, root = self.make_fake_host()
        with tmp:
            fake_udevadm = root / "bin" / "udevadm"
            fake_udevadm.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env sh
                    if [ "$1" = "info" ]; then
                        cat <<'EOF'
ID_VENDOR_ID=303a
ID_MODEL_ID=1001
ID_SERIAL_SHORT=ABC123", RUN+="/bin/sh
ID_MODEL=USB_JTAG_serial_debug_unit
EOF
                    fi
                    exit 0
                    """
                )
            )
            fake_udevadm.chmod(0o755)

            result = self.run_script(env=env)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsafe USB identity", result.stderr)
            self.assertFalse((root / "etc" / "udev" / "rules.d" / "99-esp32-kvm.rules").exists())

    def test_fails_if_udev_does_not_create_stable_symlink(self) -> None:
        tmp, env, root = self.make_fake_host()
        with tmp:
            fake_udevadm = root / "bin" / "udevadm"
            fake_udevadm.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env sh
                    if [ "$1" = "info" ]; then
                        cat <<'EOF'
ID_VENDOR_ID=303a
ID_MODEL_ID=1001
ID_SERIAL_SHORT=ABC123
ID_MODEL=USB_JTAG_serial_debug_unit
EOF
                    fi
                    exit 0
                    """
                )
            )
            fake_udevadm.chmod(0o755)

            result = self.run_script(env=env)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("udev did not create", result.stderr)


if __name__ == "__main__":
    unittest.main()
