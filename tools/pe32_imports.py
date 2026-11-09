#!/usr/bin/env python3
"""List imported Windows DLLs/functions in a 32-bit PE image.

This produces the API inventory required before replacing Windows/DirectDraw
calls. It only reads the image; no game assets are extracted or redistributed.
"""

import argparse
import json
import struct
import sys
from pathlib import Path


class PEError(ValueError):
    pass


class PEImports:
    def __init__(self, data):
        self.data = data
        if self.read(0, 2) != b'MZ':
            raise PEError('missing MZ signature')
        pe = self.u32(0x3c)
        if self.read(pe, 4) != b'PE\0\0':
            raise PEError('missing PE signature')
        coff = pe + 4
        if self.u16(coff) != 0x14c:
            raise PEError('expected i386 machine type')
        sections = self.u16(coff + 2)
        optional_size = self.u16(coff + 16)
        opt = coff + 20
        self.read(opt, optional_size)
        if optional_size < 96 or self.u16(opt) != 0x10b:
            raise PEError('expected PE32 optional header')
        self.header_size = self.u32(opt + 60)
        directories = self.u32(opt + 92)
        self.import_rva = self.import_size = 0
        if directories >= 2 and optional_size >= 112:
            self.import_rva, self.import_size = struct.unpack(
                '<II', self.read(opt + 104, 8))
        if not 0 < sections <= 96:
            raise PEError('invalid section count')
        self.sections = []
        for i in range(sections):
            section = self.read(opt + optional_size + 40 * i, 40)
            virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', section, 8)
            self.sections.append((rva, max(virtual_size, raw_size), raw, raw_size))

    def read(self, offset, length):
        if offset < 0 or length < 0 or offset > len(self.data) or length > len(self.data) - offset:
            raise PEError('access outside file')
        return self.data[offset:offset + length]

    def u16(self, offset):
        return struct.unpack('<H', self.read(offset, 2))[0]

    def u32(self, offset):
        return struct.unpack('<I', self.read(offset, 4))[0]

    def rva_to_offset(self, rva, length):
        if rva < self.header_size and rva + length <= self.header_size:
            self.read(rva, length)
            return rva
        for start, virtual_size, raw, raw_size in self.sections:
            delta = rva - start
            if 0 <= delta and delta + length <= raw_size and delta + length <= virtual_size:
                self.read(raw + delta, length)
                return raw + delta
        raise PEError(f'unbacked RVA 0x{rva:08x}')

    def cstring(self, rva):
        # Bound malformed/nonterminating strings even inside a very large PE.
        result = bytearray()
        for i in range(4096):
            byte = self.read(self.rva_to_offset(rva + i, 1), 1)[0]
            if not byte:
                try:
                    return result.decode('ascii')
                except UnicodeError as exc:
                    raise PEError('non-ASCII import name') from exc
            result.append(byte)
        raise PEError('unterminated import name')

    def imports(self):
        if not self.import_rva and not self.import_size:
            return []
        if not self.import_rva or not self.import_size:
            raise PEError('incomplete import directory')
        result = []
        max_entries = min(self.import_size // 20, 4096)
        for i in range(max_entries):
            entry = self.read(self.rva_to_offset(self.import_rva + i * 20, 20), 20)
            original, timestamp, forwarder, dll_name, first = struct.unpack('<IIIII', entry)
            if not (original or timestamp or forwarder or dll_name or first):
                return result
            if not dll_name or not first:
                raise PEError('invalid import descriptor')
            entries = []
            table = original or first
            for j in range(65536):
                value = self.u32(self.rva_to_offset(table + j * 4, 4))
                if not value:
                    break
                if value & 0x80000000:
                    entries.append({'ordinal': value & 0xffff})
                else:
                    hint = self.u16(self.rva_to_offset(value, 2))
                    entries.append({'name': self.cstring(value + 2), 'hint': hint})
            else:
                raise PEError('unterminated import lookup table')
            result.append({'dll': self.cstring(dll_name), 'symbols': entries})
        raise PEError('unterminated import descriptor table')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pe', type=Path, help='original Windows EXE or DLL')
    parser.add_argument('--json', action='store_true', help='print machine-readable inventory')
    args = parser.parse_args()
    try:
        modules = PEImports(args.pe.read_bytes()).imports()
    except (OSError, PEError) as exc:
        print(f'{parser.prog}: {exc}', file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(modules, indent=2))
    else:
        for module in modules:
            print(f"{module['dll']}: {len(module['symbols'])} imports")
            for symbol in module['symbols']:
                print(f"  {symbol['name']}" if 'name' in symbol else
                      f"  ordinal #{symbol['ordinal']}")
        print(f'{len(modules)} imported DLLs, {sum(len(m["symbols"]) for m in modules)} symbols')
    return 0


if __name__ == '__main__':
    sys.exit(main())
