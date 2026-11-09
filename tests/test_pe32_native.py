"""Synthetic PE32 fixtures for the native execution bootstrap and import tool."""

import errno
import hashlib
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


def fixture(*, image_base=0x00400000, relocated=False, imports=False,
            exports=False, call_import=False):
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
    elif call_import:
        # call dword ptr [absolute IAT address]; ret, with a HIGHLOW fixup.
        data[0x200:0x202] = b'\xff\x15'
        struct.pack_into('<I', data, 0x202, image_base + 0x2080)
        data[0x206] = 0xc3
        struct.pack_into('<II', data, opt + 136, 0x3000, 12)
        struct.pack_into('<IIHH', data, 0x600, 0x1000, 12, 0x3002, 0)
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
    if exports:
        # Synthetic "user32.dll" exports MessageBoxA at the DLL's .text RVA.
        struct.pack_into('<II', data, opt + 96, 0x2100, 0xa0)
        struct.pack_into('<IIIIIII', data, 0x500 + 12,
                         0x2170, 7, 1, 1, 0x2140, 0x2150, 0x2160)
        struct.pack_into('<I', data, 0x540, 0x1000)
        struct.pack_into('<I', data, 0x550, 0x2180)
        struct.pack_into('<H', data, 0x560, 0)
        data[0x570:0x57b] = b'user32.dll\0'
        data[0x580:0x58c] = b'MessageBoxA\0'
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

    def test_exports_by_name_and_ordinal(self):
        exports = pe32_imports.PEImports(fixture(exports=True)).exports()
        self.assertEqual(exports, {'dll': 'user32.dll', 'symbols': [
            {'ordinal': 7, 'rva': 0x1000, 'names': ['MessageBoxA']}]})
        data = bytearray(fixture(exports=True))
        struct.pack_into('<I', data, 0x500 + 24, 0)  # No named exports.
        self.assertEqual(pe32_imports.PEImports(data).exports()['symbols'], [
            {'ordinal': 7, 'rva': 0x1000, 'names': []}])

    def test_forwarded_export_and_bad_name_ordinal(self):
        data = bytearray(fixture(exports=True))
        struct.pack_into('<I', data, 0x540, 0x2190)
        data[0x590:0x5a4] = b'kernel32.SomeMethod\0'
        symbol = pe32_imports.PEImports(data).exports()['symbols'][0]
        self.assertEqual(symbol['forwarder'], 'kernel32.SomeMethod')
        struct.pack_into('<H', data, 0x560, 999)
        with self.assertRaises(pe32_imports.PEError):
            pe32_imports.PEImports(data).exports()

    def test_cli_lists_exports(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'fixture.dll'
            file.write_bytes(fixture(exports=True))
            result = subprocess.run([sys.executable, str(ROOT / 'tools/pe32_imports.py'),
                                     '--exports', str(file)], capture_output=True,
                                    text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('DLL export name: user32.dll', result.stdout)
            self.assertIn('#7: MessageBoxA', result.stdout)

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
        self.assertIn('module imports not fully resolved', result.stderr)

    def test_two_modules_call_by_import(self):
        if not self.runner.is_file():
            self.skipTest('build with scripts/build-native-pe32.sh first')
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'fixture.exe'
            dll = Path(directory) / 'fake-user32.dll'
            exe.write_bytes(fixture(imports=True, call_import=True))
            dll.write_bytes(fixture(image_base=0x10000000, exports=True))
            command = ([self.qemu] if self.qemu else []) + [
                str(self.runner), str(exe), str(dll), '--run-entry']
            try:
                result = subprocess.run(command, capture_output=True, text=True,
                                        check=False)
            except OSError as exc:
                if exc.errno == errno.ENOEXEC:
                    self.skipTest('host kernel has no i386 ELF support; install qemu-i386')
                raise
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('linked user32.dll!MessageBoxA', result.stdout)
            self.assertIn('Entry returned 42', result.stdout)


class HostLinkerTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host C compiler unavailable')
    def test_c_linker_with_host_abi(self):
        with tempfile.TemporaryDirectory() as directory:
            runner = Path(directory) / 'link-test'
            build = subprocess.run([
                'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-I', str(ROOT / 'src'),
                str(ROOT / 'src/pe32_link.c'),
                str(ROOT / 'tests/test_pe32_link.c'), '-o', str(runner)],
                capture_output=True, text=True, check=False)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(runner)], capture_output=True,
                                    text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('tests passed', result.stdout)


class OriginalDiscTests(unittest.TestCase):
    """Optional read-only regression checks; proprietary binaries stay ignored."""

    def test_verified_1997_disc_exports_and_imports(self):
        exe = ROOT / 'data/disc/A-10Cuba/A10Cuba.exe'
        dll = ROOT / 'data/disc/A-10Cuba/A10Sim.dll'
        if not exe.is_file() or not dll.is_file():
            self.skipTest('original disc binaries not available in ignored data/disc')
        exe_data, dll_data = exe.read_bytes(), dll.read_bytes()
        if hashlib.sha256(dll_data).hexdigest() != (
            '3aefdc3f7c41bee8ae99d286230637b893ad22c027c99086ad33a6467c607f51'
        ):
            self.skipTest('different original DLL revision; this witness is disc-specific')
        if hashlib.sha256(exe_data).hexdigest() != (
            '5af0a01a839338ec1b599b43d86fdc0897ba11dd24e660eaafc0ff1e559cde55'
        ):
            self.skipTest('different original EXE revision; this witness is disc-specific')
        exe_imports = pe32_imports.PEImports(exe_data).imports()
        dll_imports = pe32_imports.PEImports(dll_data).imports()
        exports = pe32_imports.PEImports(dll_data).exports()
        self.assertEqual((len(exe_imports), sum(len(x['symbols']) for x in exe_imports)),
                         (15, 335))
        self.assertEqual((len(dll_imports), sum(len(x['symbols']) for x in dll_imports)),
                         (2, 61))
        self.assertEqual(exports['dll'], 'A10Sim.dll')
        self.assertEqual(
            [(x['names'], x['rva']) for x in exports['symbols']], [
                (['_SIM_Free@4'], 0x13620),
                (['_SIM_GetName@4'], 0x13630),
                (['_SIM_Init@4'], 0x13670),
                (['_SIM_Malloc@4'], 0x13690),
                (['_SIM_ProcessEvent@16'], 0x136a0),
                (['_SIM_Start@4'], 0x136c0),
            ])


if __name__ == '__main__':
    unittest.main()
