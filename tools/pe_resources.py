#!/usr/bin/env python3
"""List or extract individual resources from a 32-bit Windows PE file."""
import argparse
import json
import struct
import sys
from pathlib import Path


class PEError(ValueError):
    pass


class PE:
    def __init__(self, data):
        self.data = data
        if self.read(0, 2) != b'MZ':
            raise PEError('missing DOS signature')
        pe = self.u32(0x3c)
        if self.read(pe, 4) != b'PE\0\0':
            raise PEError('missing PE signature')
        coff = pe + 4
        sections = self.u16(coff + 2)
        opt_size = self.u16(coff + 16)
        opt = coff + 20
        self.read(opt, opt_size)
        if self.u16(opt) != 0x10b or opt_size < 120:
            raise PEError('expected PE32 optional header with resource directory')
        if self.u32(opt + 92) < 3:
            raise PEError('PE has no resource data directory')
        resource_rva, self.resource_size = struct.unpack('<II', self.read(opt + 112, 8))
        if not resource_rva or not self.resource_size:
            raise PEError('PE has no resources')
        self.sections = []
        for i in range(sections):
            sh = self.read(opt + opt_size + i * 40, 40)
            virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', sh, 8)
            self.sections.append((rva, virtual_size, raw, raw_size))
        self.base = self.rva_offset(resource_rva, self.resource_size)

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise PEError('offset outside file')
        return self.data[offset:offset + size]

    def u16(self, offset):
        return struct.unpack('<H', self.read(offset, 2))[0]

    def u32(self, offset):
        return struct.unpack('<I', self.read(offset, 4))[0]

    def rva_offset(self, rva, size):
        for start, virtual_size, raw, raw_size in self.sections:
            delta = rva - start
            if 0 <= delta and delta + size <= raw_size and delta < max(virtual_size, raw_size):
                self.read(raw + delta, size)
                return raw + delta
        raise PEError('resource RVA is not backed by section data')

    def resource_offset(self, relative, size):
        if relative < 0 or size < 0 or relative + size > self.resource_size:
            raise PEError('resource directory offset outside resource section')
        return self.base + relative

    def name(self, entry):
        if not entry & 0x80000000:
            return entry & 0xffff
        pos = self.resource_offset(entry & 0x7fffffff, 2)
        length = self.u16(pos)
        self.resource_offset((entry & 0x7fffffff) + 2, length * 2)
        return self.read(pos + 2, length * 2).decode('utf-16le')

    def resources(self):
        def walk(relative, path, parents):
            if relative in parents or len(path) > 2:
                raise PEError('cyclic or unexpectedly deep resource directory')
            pos = self.resource_offset(relative, 16)
            count = self.u16(pos + 12) + self.u16(pos + 14)
            self.resource_offset(relative + 16, count * 8)
            for i in range(count):
                key, target = struct.unpack('<II', self.read(pos + 16 + i * 8, 8))
                new_path = path + (self.name(key),)
                if target & 0x80000000:
                    yield from walk(target & 0x7fffffff, new_path, parents | {relative})
                else:
                    if len(new_path) != 3:
                        raise PEError('unexpected resource leaf depth')
                    leaf = self.resource_offset(target, 16)
                    rva, size, codepage, _ = struct.unpack('<IIII', self.read(leaf, 16))
                    offset = self.rva_offset(rva, size)
                    yield {'type': new_path[0], 'name': new_path[1], 'language': new_path[2],
                           'rva': rva, 'size': size, 'codepage': codepage, 'offset': offset}
        yield from walk(0, (), set())


def key(value):
    return int(value[1:]) if value.startswith('#') else value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pe', type=Path)
    parser.add_argument('--type', help='resource type; use #NUMBER for numeric IDs')
    parser.add_argument('--name', help='resource name; use #NUMBER for numeric IDs')
    parser.add_argument('--language', type=int, help='numeric language ID')
    parser.add_argument('--extract', type=Path, help='write exactly one selected resource without overwriting')
    args = parser.parse_args()
    try:
        data = args.pe.read_bytes()
        entries = list(PE(data).resources())
        for field in ('type', 'name'):
            value = getattr(args, field)
            if value is not None:
                wanted = key(value)
                entries = [item for item in entries if item[field] == wanted]
        if args.language is not None:
            entries = [item for item in entries if item['language'] == args.language]
        if args.extract:
            if len(entries) != 1:
                raise PEError('extraction requires exactly one matching resource')
            item = entries[0]
            with args.extract.open('xb') as output:
                output.write(data[item['offset']:item['offset'] + item['size']])
        else:
            print(json.dumps(entries, indent=2))
    except (OSError, PEError, UnicodeError, ValueError) as exc:
        print(f'{parser.prog}: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
