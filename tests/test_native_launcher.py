# SPDX-FileCopyrightText: 2026 Ingo Ruhnke
# SPDX-License-Identifier: GPL-3.0-or-later

"""Verify the Nix-facing native launcher without Nix or i386 execution."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "scripts/a10cuba-native.sh"


class NativeLauncherTests(unittest.TestCase):
    def run_launcher(self, *args, **environment):
        env = os.environ.copy()
        env.update(environment)
        return subprocess.run(
            ["sh", str(LAUNCHER), *args],
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_help_and_shell_syntax(self):
        result = subprocess.run(["sh", "-n", str(LAUNCHER)], check=False)
        self.assertEqual(result.returncode, 0)
        result = self.run_launcher("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("nix run .#a10cuba-native", result.stdout)
        self.assertIn("--link", result.stdout)

    def test_missing_default_data_is_explained(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_launcher(A10_CUBA_DATA_DIR=directory)
        self.assertEqual(result.returncode, 1)
        self.assertIn("original game binaries not found", result.stderr)

    def test_default_data_and_link_option_preserve_spaces(self):
        echo = shutil.which("echo")
        self.assertIsNotNone(echo)
        with tempfile.TemporaryDirectory(prefix="a10 cuba ") as directory:
            data = Path(directory)
            (data / "A10Cuba.exe").touch()
            (data / "A10Sim.dll").touch()
            env = {"A10_CUBA_DATA_DIR": directory, "A10_PE32_NATIVE_BIN": echo}
            expected = f"{data / 'A10Cuba.exe'} {data / 'A10Sim.dll'}"
            result = self.run_launcher(**env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), expected)
            result = self.run_launcher("--link", **env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), expected + " --link")

    def test_explicit_images_bypass_data_discovery(self):
        echo = shutil.which("echo")
        self.assertIsNotNone(echo)
        result = self.run_launcher(
            "/tmp/custom image.exe", "/tmp/custom dll.dll", "--link",
            A10_CUBA_DATA_DIR="/nonexistent/a10cuba",
            A10_PE32_NATIVE_BIN=echo,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout.strip(),
            "/tmp/custom image.exe /tmp/custom dll.dll --link",
        )


if __name__ == "__main__":
    unittest.main()
