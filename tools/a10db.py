#!/usr/bin/env python3
"""Reader for A-10 Cuba! ``*.db`` mission/model/terrain databases.

Format notes live in docs/mission-format.md.  Usage:

    python3 -I tools/a10db.py FILE.db [--depth N] [--props] [--paths]
"""
import argparse
import bisect
import struct

MAGIC = 0x1A4F92ED
NODE_GROUP = 1
NODE_LEAF = 2
CHILD_SIZE = 0x24


class Node:
    def __init__(self, offset, kind, flags, name, count, aux, size, child_array,
                 prop_ptr):
        self.offset = offset
        self.kind = kind
        self.flags = flags
        self.name = name
        self.count = count
        self.aux = aux
        self.size = size
        self.child_array = child_array
        self.prop_ptr = prop_ptr
        self.children = []  # list of (transform, Node) for groups


class Database:
    def __init__(self, data):
        self.data = data
        magic, self.data_off, self.symsz, self.symoff, self.footer_count, \
            self.footer_off = struct.unpack_from('<IIHIHI', data, 0)
        if magic != MAGIC:
            raise ValueError('not an A-10 Cuba db (bad magic)')
        self.nodes = {}
        self.root = self._node(self.data_off)

    @classmethod
    def open(cls, path):
        with open(path, 'rb') as f:
            return cls(f.read())

    def u32(self, off):
        return struct.unpack_from('<I', self.data, off)[0]

    def symbol(self, sid):
        """Symbol string for a 16-bit symbol-table offset, or None."""
        if sid >= self.symsz:
            return None
        start = self.symoff + sid
        end = self.data.index(b'\0', start)
        return self.data[start:end].decode('latin1')

    def symbols(self):
        raw = self.data[self.symoff:self.symoff + self.symsz]
        out, pos = {}, 0
        for part in raw.split(b'\0'):
            if part:
                out[pos] = part.decode('latin1')
            pos += len(part) + 1
        return out

    def _node(self, off):
        if off in self.nodes:
            return self.nodes[off]
        d = self.data
        kind, flags, name, count, aux = struct.unpack_from('<BBHHH', d, off)
        size = self.u32(off + 8)
        child_array = self.u32(off + 12)
        prop_ptr = self.u32(off + 0x10) if kind == NODE_GROUP else 0
        node = Node(off, kind, flags, self.symbol(name), count, aux, size,
                    child_array, prop_ptr)
        self.nodes[off] = node
        if kind == NODE_GROUP:
            for i in range(count):
                ent = child_array + i * CHILD_SIZE
                words = struct.unpack_from('<8i', d, ent)
                child = self._node(self.u32(ent + 0x20))
                node.children.append((Transform(words), child))
        return node

    # -- property token streams ------------------------------------------
    def _boundaries(self):
        if not hasattr(self, '_bounds'):
            b = {self.symoff}
            for n in self.nodes.values():
                b.add(n.offset)
                if n.kind == NODE_GROUP:
                    b.add(n.child_array)
                    if n.prop_ptr:
                        b.add(n.prop_ptr)
                else:
                    for k in (0x14, 0x18, 0x1C, 0x20):
                        p = self.u32(n.offset + k)
                        if p:
                            b.add(p)
            self._bounds = sorted(b)
        return self._bounds

    def decode_tokens(self, start, end):
        """Decode a token stream: word = symbol | argcount << 16, then args."""
        words = [self.u32(o) for o in range(start, end, 4)]
        out, i = [], 0
        while i < len(words):
            w = words[i]
            name = self.symbol(w & 0xFFFF)
            nargs = w >> 16
            if not name or not name.replace('_', '').isalnum() or \
                    i + nargs >= len(words) + 0:
                break
            args = [x - (1 << 32) if x >= 1 << 31 else x
                    for x in words[i + 1:i + 1 + nargs]]
            out.append((name, args))
            i += 1 + nargs
        return out

    def properties(self, node):
        """Property tokens of a group node (empty list if none)."""
        if node.kind != NODE_GROUP or not node.prop_ptr:
            return []
        bounds = self._boundaries()
        end = bounds[bisect.bisect_right(bounds, node.prop_ptr)]
        return self.decode_tokens(node.prop_ptr, end)

    def path_script(self, node):
        """Waypoint script tokens of a leaf (data at node+0x20)."""
        if node.kind != NODE_LEAF:
            return []
        start = self.u32(node.offset + 0x20)
        end = self.u32(node.offset + 0x14)  # LOD0 follows the script
        return self.decode_tokens(start, end) if 0 < start < end else []


class Transform:
    """Child placement: Y rotation (2.30 fixed point) plus position."""

    def __init__(self, words):
        self.words = words
        self.cos, self.sin = words[0] / 2**30, words[1] / 2**30
        self.pos = tuple(words[5:8])

    @property
    def heading_deg(self):
        import math
        return math.degrees(math.atan2(self.sin, self.cos))


def dump(db, node, depth, props, paths, indent=0, seen=None):
    seen = set() if seen is None else seen
    pad = '  ' * indent
    kind = 'G' if node.kind == NODE_GROUP else 'L'
    print('%s%s %s @%#x' % (pad, kind, node.name, node.offset))
    if props:
        for name, args in db.properties(node):
            print('%s    . %s%s' % (pad, name, args if args else ''))
    if paths and node.kind == NODE_LEAF:
        for name, args in db.path_script(node):
            print('%s    ~ %s%s' % (pad, name, args if args else ''))
    if node.offset in seen or indent >= depth:
        return
    seen.add(node.offset)
    for tr, child in node.children:
        print('%s  pos=%s hdg=%.1f' % (pad, tr.pos, tr.heading_deg))
        dump(db, child, depth, props, paths, indent + 1, seen)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('db')
    ap.add_argument('--depth', type=int, default=3)
    ap.add_argument('--props', action='store_true')
    ap.add_argument('--paths', action='store_true')
    a = ap.parse_args()
    db = Database.open(a.db)
    dump(db, db.root, a.depth, a.props, a.paths)


if __name__ == '__main__':
    main()
