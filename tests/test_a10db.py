import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
import a10db  # noqa: E402

ONE = 0x40000000


def build_db():
    """Tiny synthetic db: root group -> one leaf-less child group with props."""
    syms = b'root\0unit\0pilot\0time\0'
    off = {n: syms.index(n.encode() + b'\0') for n in ('root', 'unit', 'pilot', 'time')}
    base = 0x14
    root = base
    root_arr = root + 0x20
    unit = root_arr + 0x24
    unit_arr = unit + 0x20
    props = unit_arr + 0x24
    prop_words = [off['pilot'], off['time'] | 3 << 16, 0, 1, 30]
    props_end = props + 4 * len(prop_words)
    symoff = props_end
    buf = bytearray(symoff + len(syms))
    struct.pack_into('<IIHIHI', buf, 0, a10db.MAGIC, 0x14, len(syms), symoff, 0, 0)
    struct.pack_into('<BBHHHI I', buf, root, 1, 0, off['root'], 1, 0, 2, root_arr)
    struct.pack_into('<9i', buf, root_arr, ONE, 0, ONE, 0, ONE, 100, 200, 300, unit)
    struct.pack_into('<BBHHHI I I', buf, unit, 1, 0, off['unit'], 0, 0, 1,
                     unit_arr, props)
    struct.pack_into('<%dI' % len(prop_words), buf, props, *prop_words)
    buf[symoff:] = syms
    return bytes(buf)


class A10DbTest(unittest.TestCase):
    def setUp(self):
        self.db = a10db.Database(build_db())

    def test_bad_magic(self):
        with self.assertRaises(ValueError):
            a10db.Database(b'\0' * 32)

    def test_tree_and_transform(self):
        root = self.db.root
        self.assertEqual(root.name, 'root')
        tr, unit = root.children[0]
        self.assertEqual(unit.name, 'unit')
        self.assertEqual(tr.pos, (100, 200, 300))
        self.assertAlmostEqual(tr.heading_deg, 0.0)

    def test_properties(self):
        _, unit = self.db.root.children[0]
        self.assertEqual(self.db.properties(unit),
                         [('pilot', []), ('time', [0, 1, 30])])

    def test_symbols(self):
        self.assertEqual(self.db.symbols()[0], 'root')


if __name__ == '__main__':
    unittest.main()
