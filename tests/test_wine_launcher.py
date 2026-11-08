import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/a10cuba-wine.sh'


class WineLauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        wine = self.bin / 'wine'
        wine.write_text('''#!/bin/sh
if [ "$1" = reg ]; then
    printf '%s\\n' "$*" >> "$TEST_REG_LOG"
    if [ "$2" = query ]; then
        case "$3" in *A-10Button*) exit 0;; *) exit 1;; esac
    fi
    exit 0
fi
printf '%s\\n%s\\n%s\\n' "$PWD" "$WINEPREFIX" "$*" > "$TEST_LOG"
''')
        wine.chmod(0o755)
        winepath = self.bin / 'winepath'
        winepath.write_text('#!/bin/sh\nprintf "Z:\\\\fake.wav\\n"\n')
        winepath.chmod(0o755)
        self.disc = self.root / 'disc'
        (self.disc / 'A-10Cuba/Missions').mkdir(parents=True)
        (self.disc / 'A-10Cuba/LoadOuts').mkdir()
        (self.disc / 'A-10Cuba/Media').mkdir()
        for filename in ('APU.wav', 'SwitchPush.wav'):
            (self.disc / 'A-10Cuba/Media' / filename).write_text('wav')
        self.sound_map = self.root / 'sounds.txt'
        self.sound_map.write_text('A-10APU APU.wav\nA-10Button SwitchPush.wav\n')
        (self.disc / 'A-10Cuba/LoadOuts/Default.lod').write_text('default')
        for name in ('A-10Cuba/A10Cuba.exe', 'A-10Cuba/A10Sim.dll',
                     'A-10Cuba/Missions/CubaTerrain.db', 'CTL3D32.DLL'):
            (self.disc / name).write_text(name)
        self.env = os.environ.copy()
        self.env.update(PATH=f'{self.bin}:{self.env["PATH"]}', HOME=str(self.root),
                        XDG_STATE_HOME=str(self.root / 'custom-state'), XDG_DATA_HOME=str(self.root / 'custom-data'), TEST_LOG=str(self.root / 'wine.log'),
                        TEST_REG_LOG=str(self.root / 'registry.log'), A10_CUBA_SOUND_MAP=str(self.sound_map))

    def run_launcher(self, *args, cwd=None):
        return subprocess.run([str(SCRIPT), *map(str, args)], cwd=cwd or self.root,
                              env=self.env, text=True, capture_output=True)

    def test_setup_and_reuse_preserve_game_state(self):
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        state = self.root / 'custom-state/a10cuba-wine'
        self.assertEqual((self.root / 'wine.log').read_text().splitlines(),
                         [str(state / 'game'), str(state / 'prefix'), 'explorer /desktop=A10Cuba,1024x768 A10Cuba.exe'])
        self.assertTrue((self.root / 'custom-data/a10cuba-wine/game/A10Cuba.exe').is_file())
        self.assertTrue((state / 'game/A10Cuba.exe').is_symlink())
        loadout = state / 'game/LoadOuts/Default.lod'
        self.assertFalse(loadout.is_symlink())
        loadout.write_text('modified')
        self.assertEqual((self.root / 'custom-data/a10cuba-wine/game/LoadOuts/Default.lod').read_text(), 'default')
        registry_log = (self.root / 'registry.log').read_text()
        self.assertIn('add HKCU', registry_log)
        self.assertIn('A-10APU', registry_log)
        self.assertNotIn('add HKCU\\AppEvents\\Schemes\\Apps\\A10Cuba\\A-10Button', registry_log)
        self.assertTrue((state / '.sound-events-v1').is_file())
        save = state / 'game/save.dat'
        save.write_text('progress')
        (self.disc / 'A-10Cuba/A10Cuba.exe').unlink()
        result = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(save.read_text(), 'progress')
        self.assertEqual((self.root / 'registry.log').read_text(), registry_log)
        self.assertTrue((state / 'home').is_dir())
        self.assertTrue((state / 'xdg/config').is_dir())
        self.assertEqual(list(state.glob('.setup.????????')), [])

    def test_missing_disc_leaves_no_partial_game(self):
        (self.disc / 'A-10Cuba/A10Sim.dll').unlink()
        result = self.run_launcher('--disc', self.disc)
        self.assertNotEqual(result.returncode, 0)
        state = self.root / 'custom-state/a10cuba-wine'
        self.assertFalse((state / 'game').exists())
        self.assertFalse((self.root / 'custom-data/a10cuba-wine/game').exists())
        self.assertEqual(list(state.glob('.setup.????????')), [])

    def test_old_xdg_directories_are_not_migrated(self):
        old_state = self.root / 'custom-state/a10-cuba-wine/game'
        old_state.mkdir(parents=True)
        (old_state / 'save.dat').write_text('old save')
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((old_state / 'save.dat').read_text(), 'old save')
        self.assertFalse((self.root / 'custom-state/a10cuba-wine/game/save.dat').exists())

    def test_custom_desktop_size(self):
        self.env['A10_CUBA_DESKTOP_SIZE'] = '1280x800'
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('/desktop=A10Cuba,1280x800', (self.root / 'wine.log').read_text())

    def test_relative_xdg_state_home_is_ignored(self):
        self.env['XDG_STATE_HOME'] = 'relative'
        result = self.run_launcher('--disc', self.disc)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / '.local/state/a10cuba-wine/game/A10Cuba.exe').exists())


if __name__ == '__main__':
    unittest.main()
