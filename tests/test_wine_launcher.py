import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/a10-cube-wine.sh'


class WineLauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        wine = self.bin / 'wine'
        wine.write_text('#!/bin/sh\nprintf "%s\\n%s\\n%s\\n" "$PWD" "$WINEPREFIX" "$*" > "$TEST_LOG"\n')
        wine.chmod(0o755)
        self.disc = self.root / 'disc'
        (self.disc / 'A-10Cuba/Missions').mkdir(parents=True)
        for name in ('A-10Cuba/A10Cuba.exe', 'A-10Cuba/A10Sim.dll',
                     'A-10Cuba/Missions/CubaTerrain.db', 'CTL3D32.DLL'):
            (self.disc / name).write_text(name)
        self.env = os.environ.copy()
        self.env.update(PATH=f'{self.bin}:{self.env["PATH"]}', HOME=str(self.root),
                        XDG_STATE_HOME=str(self.root / 'custom-state'), TEST_LOG=str(self.root / 'wine.log'))

    def run_launcher(self, *args, cwd=None):
        return subprocess.run([str(SCRIPT), *map(str, args)], cwd=cwd or self.root,
                              env=self.env, text=True, capture_output=True)

    def test_setup_and_reuse_preserve_game_state(self):
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        state = self.root / 'custom-state/a10-cuba-wine'
        self.assertEqual((self.root / 'wine.log').read_text().splitlines(),
                         [str(state / 'game'), str(state / 'prefix'), 'A10Cuba.exe'])
        save = state / 'game/save.dat'
        save.write_text('progress')
        (self.disc / 'A-10Cuba/A10Cuba.exe').unlink()
        result = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(save.read_text(), 'progress')
        self.assertTrue((state / 'home').is_dir())
        self.assertTrue((state / 'xdg/config').is_dir())
        self.assertEqual(list(state.glob('.setup.????????')), [])

    def test_missing_disc_leaves_no_partial_game(self):
        (self.disc / 'A-10Cuba/A10Sim.dll').unlink()
        result = self.run_launcher('--disc', self.disc)
        self.assertNotEqual(result.returncode, 0)
        state = self.root / 'custom-state/a10-cuba-wine'
        self.assertFalse((state / 'game').exists())
        self.assertEqual(list(state.glob('.setup.????????')), [])

    def test_relative_xdg_state_home_is_ignored(self):
        self.env['XDG_STATE_HOME'] = 'relative'
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / '.local/state/a10-cuba-wine/game/A10Cuba.exe').exists())


if __name__ == '__main__':
    unittest.main()
