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
        (self.disc / 'A-10Cuba/LoadOuts').mkdir()
        (self.disc / 'A-10Cuba/LoadOuts/Default.lod').write_text('default')
        for name in ('A-10Cuba/A10Cuba.exe', 'A-10Cuba/A10Sim.dll',
                     'A-10Cuba/Missions/CubaTerrain.db', 'CTL3D32.DLL'):
            (self.disc / name).write_text(name)
        self.env = os.environ.copy()
        self.env.update(PATH=f'{self.bin}:{self.env["PATH"]}', HOME=str(self.root),
                        XDG_STATE_HOME=str(self.root / 'custom-state'), XDG_DATA_HOME=str(self.root / 'custom-data'), TEST_LOG=str(self.root / 'wine.log'))

    def run_launcher(self, *args, cwd=None):
        return subprocess.run([str(SCRIPT), *map(str, args)], cwd=cwd or self.root,
                              env=self.env, text=True, capture_output=True)

    def test_setup_and_reuse_preserve_game_state(self):
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        state = self.root / 'custom-state/a10-cuba-wine'
        self.assertEqual((self.root / 'wine.log').read_text().splitlines(),
                         [str(state / 'game'), str(state / 'prefix'), 'explorer /desktop=A10Cuba,1024x768 A10Cuba.exe'])
        self.assertTrue((self.root / 'custom-data/a10-cuba-wine/game/A10Cuba.exe').is_file())
        self.assertTrue((state / 'game/A10Cuba.exe').is_symlink())
        loadout = state / 'game/LoadOuts/Default.lod'
        self.assertFalse(loadout.is_symlink())
        loadout.write_text('modified')
        self.assertEqual((self.root / 'custom-data/a10-cuba-wine/game/LoadOuts/Default.lod').read_text(), 'default')
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
        self.assertFalse((self.root / 'custom-data/a10-cuba-wine/game').exists())
        self.assertEqual(list(state.glob('.setup.????????')), [])

    def test_existing_working_directory_preserves_saves(self):
        state_game = self.root / 'custom-state/a10-cuba-wine/game'
        state_game.mkdir(parents=True)
        for name in ('A10Cuba.exe', 'A10Sim.dll', 'existing-save.dat'):
            (state_game / name).write_text(name)
        result = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((state_game / 'existing-save.dat').read_text(), 'existing-save.dat')
        self.assertFalse((state_game / 'A10Cuba.exe').is_symlink())
        self.assertTrue((self.root / 'custom-data/a10-cuba-wine/game/A10Cuba.exe').is_file())
        self.assertFalse((self.root / 'custom-data/a10-cuba-wine/game/existing-save.dat').exists())

    def test_custom_desktop_size(self):
        self.env['A10_CUBA_DESKTOP_SIZE'] = '1280x800'
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('/desktop=A10Cuba,1280x800', (self.root / 'wine.log').read_text())

    def test_relative_xdg_state_home_is_ignored(self):
        self.env['XDG_STATE_HOME'] = 'relative'
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / '.local/state/a10-cuba-wine/game/A10Cuba.exe').exists())


if __name__ == '__main__':
    unittest.main()
