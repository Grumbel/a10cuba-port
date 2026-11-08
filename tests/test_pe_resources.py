import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('pe_resources', Path(__file__).resolve().parents[1] / 'tools/pe_resources.py')
pe_resources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pe_resources)


def fixture():
    data = bytearray(0x400)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    coff = 0x84
    struct.pack_into('<H', data, coff + 2, 1)
    struct.pack_into('<H', data, coff + 16, 0xe0)
    opt = coff + 20
    struct.pack_into('<H', data, opt, 0x10b)
    struct.pack_into('<I', data, opt + 92, 16)
    struct.pack_into('<II', data, opt + 112, 0x1000, 0x200)
    struct.pack_into('<IIII', data, opt + 0xe0 + 8, 0x200, 0x1000, 0x200, 0x200)
    for offset, name, target in ((0, 2, 0x80000020), (0x20, 7, 0x80000040), (0x40, 1033, 0x60)):
        struct.pack_into('<H', data, 0x200 + offset + 14, 1)
        struct.pack_into('<II', data, 0x200 + offset + 16, name, target)
    struct.pack_into('<IIII', data, 0x260, 0x1100, 4, 0, 0)
    data[0x300:0x304] = b'TEST'
    return data


class ResourceTests(unittest.TestCase):
    def test_numeric_tree_and_payload(self):
        data = fixture()
        entries = list(pe_resources.PE(data).resources())
        self.assertEqual(len(entries), 1)
        entry = entries[0]
        self.assertEqual((entry['type'], entry['name'], entry['language']), (2, 7, 1033))
        self.assertEqual(data[entry['offset']:entry['offset'] + entry['size']], b'TEST')

    def test_named_resource(self):
        data = fixture()
        struct.pack_into('<I', data, 0x210, 0x80000080)
        struct.pack_into('<H', data, 0x280, 3)
        data[0x282:0x288] = 'DIB'.encode('utf-16le')
        self.assertEqual(next(pe_resources.PE(data).resources())['type'], 'DIB')

    def test_rejects_cycle(self):
        data = fixture()
        struct.pack_into('<I', data, 0x214, 0x80000000)
        with self.assertRaises(pe_resources.PEError):
            list(pe_resources.PE(data).resources())

    def test_rejects_unbacked_payload(self):
        data = fixture()
        struct.pack_into('<I', data, 0x260, 0x3000)
        with self.assertRaises(pe_resources.PEError):
            list(pe_resources.PE(data).resources())


if __name__ == '__main__':
    unittest.main()
