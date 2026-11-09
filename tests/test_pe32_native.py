"""Synthetic PE32 fixtures for the native execution bootstrap and import tool."""

import errno
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pe32_imports', ROOT / 'tools/pe32_imports.py')
pe32_imports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pe32_imports)


def fixture(*, image_base=0x00400000, relocated=False, imports=False):
    """Generate a tiny but structurally valid 32-bit PE with a real x86 entry."""
    data = bytearray(0x800)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    coff = 0x84
    struct.pack_into('<HHIIIHH', data, coff, 0x14c, 3, 0, 0, 0, 0xe0, 0x0102)
    opt = coff + 20
    struct.pack_into('<H', data, opt, 0x10b)
    struct.pack_into('<I', data, opt + 16, 0x1000)  # entry RVA
    struct.pack_into('<I', data, opt + 20, 0x1000)  # code RVA
    struct.pack_into('<I', data, opt + 24, 0x2000)  # data RVA
    struct.pack_into('<I', data, opt + 28, image_base)
    struct.pack_into('<II', data, opt + 32, 0x1000, 0x200)
    struct.pack_into('<II', data, opt + 56, 0x4000, 0x200)
    struct.pack_into('<I', data, opt + 92, 16)
    for i, (name, rva, raw, flags) in enumerate((
        (b'.text', 0x1000, 0x200, 0x60000020),
        (b'.data', 0x2000, 0x400, 0xc0000040),
        (b'.reloc', 0x3000, 0x600, 0x42000040),
    )):
        pos = opt + 0xe0 + 40 * i
        data[pos:pos + len(name)] = name
        struct.pack_into('<IIII', data, pos + 8, 0x200, rva, 0x200, raw)
        struct.pack_into('<I', data, pos + 36, flags)

    if relocated:
        # mov eax, [absolute address of data]; ret. Relocate the imm32 operand.
        data[0x200] = 0xa1
        struct.pack_into('<I', data, 0x201, image_base + 0x2000)
        data[0x205] = 0xc3
        struct.pack_into('<II', data, opt + 136, 0x3000, 12)
        struct.pack_into('<IIHH', data, 0x600, 0x1000, 12, 0x3001, 0)
    else:
        # mov eax, 42; ret. No imported functions or runtime assumptions.
        data[0x200:0x206] = b'\xb8\x2a\x00\x00\x00\xc3'
    struct.pack_into('<I', data, 0x400, 42)

    if imports:
        struct.pack_into('<II', data, opt + 104, 0x2020, 40)
        struct.pack_into('<IIIII', data, 0x420,
                         0x2080, 0, 0, 0x20a0, 0x2080)
        struct.pack_into('<II', data, 0x480, 0x20c0, 0)
        data[0x4a0:0x4ab] = b'user32.dll\0'
        struct.pack_into('<H', data, 0x4c0, 7)
        data[0x4c2:0x4ce] = b'MessageBoxA\0'
    return bytes(data)


class ImportTests(unittest.TestCase):
    def test_no_imports(self):
        self.assertEqual(pe32_imports.PEImports(fixture()).imports(), [])

    def test_named_import(self):
        imports = pe32_imports.PEImports(fixture(imports=True)).imports()
        self.assertEqual(imports, [{'dll': 'user32.dll',
                                    'symbols': [{'name': 'MessageBoxA', 'hint': 7}]}])

    def test_ordinal_import(self):
        data = bytearray(fixture(imports=True))
        struct.pack_into('<I', data, 0x480, 0x80000021)
        imports = pe32_imports.PEImports(data).imports()
        self.assertEqual(imports[0]['symbols'], [{'ordinal': 33}])

    def test_invalid_import_rva(self):
        data = bytearray(fixture(imports=True))
        struct.pack_into('<I', data, 0x424, 0xabcdef00)  # unused timestamp
        struct.pack_into('<I', data, 0x42c, 0x800000)    # invalid DLL name RVA
        with self.assertRaises(pe32_imports.PEError):
            pe32_imports.PEImports(data).imports()

    def test_reject_truncated_pe(self):
        with self.assertRaises(pe32_imports.PEError):
            pe32_imports.PEImports(b'MZ')

    def test_cli_lists_imports(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'fixture.exe'
            file.write_bytes(fixture(imports=True))
            result = subprocess.run([sys.executable, str(ROOT / 'tools/pe32_imports.py'),
                                     str(file)], capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('user32.dll: 1 imports', result.stdout)
            self.assertIn('MessageBoxA', result.stdout)

    @unittest.skipUnless(shutil.which('objdump'), 'binutils objdump unavailable')
    def test_fixture_is_recognized_by_binutils(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'fixture.exe'
            file.write_bytes(fixture(relocated=True))
            result = subprocess.run(['objdump', '-f', str(file)],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('pei-i386', result.stdout)


class NativeRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = Path(os.environ.get('A10_PE32_RUNNER', ROOT / 'build/a10-pe32-native'))
        cls.qemu = shutil.which('qemu-i386')

    def invoke(self, image, *options):
        if not self.runner.is_file():
            self.skipTest('build with scripts/build-native-pe32.sh first')
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'fixture.exe'
            file.write_bytes(image)
            command = ([self.qemu] if self.qemu else []) + [str(self.runner), str(file), *options]
            try:
                return subprocess.run(command, capture_output=True, text=True, check=False)
            except OSError as exc:
                if exc.errno == errno.ENOEXEC:
                    self.skipTest('host kernel has no i386 ELF support; install qemu-i386 for test')
                raise

    def test_native_entry(self):
        result = self.invoke(fixture(), '--run-entry')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Entry returned 42', result.stdout)

    def test_base_relocations(self):
        # The i386 ELF bootstrap itself occupies 0x08048000, forcing a rebase.
        result = self.invoke(fixture(image_base=0x08048000, relocated=True),
                             '--run-entry')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Entry returned 42', result.stdout)
        self.assertNotIn('mapped=0x08048000', result.stdout)

    def test_imports_are_rejected_for_execution(self):
        result = self.invoke(fixture(imports=True), '--run-entry')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('cannot execute imports or TLS', result.stderr)


if __name__ == '__main__':
    unittest.main()
