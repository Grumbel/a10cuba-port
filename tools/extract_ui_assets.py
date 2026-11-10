#!/usr/bin/env python3
"""Extract the original UI bitmaps and dialog layouts for a native UI rebuild.

Writes <out>/bmp/NAME.bmp for every DIB resource (the resource already
holds a complete BMP file) and <out>/dialogs.json with the DIALOG and
DIALOGEX templates (size and controls, in dialog units). The output is
copyrighted game data: keep it in the ignored data/ directory.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_resources import PE, PEError  # noqa: E402

RT_DIALOG = 5


def wide_or_ordinal(data, pos):
    if data[pos:pos + 2] == b'\xff\xff':
        return struct.unpack_from('<H', data, pos + 2)[0], pos + 4
    end = pos
    while data[end:end + 2] != b'\0\0':
        end += 2
    return data[pos:end].decode('utf-16le'), end + 2


def parse_dialog(data):
    """Parse a DIALOG or DIALOGEX template into a dictionary."""
    extended = data[:4] == b'\x01\x00\xff\xff'
    if extended:
        style, = struct.unpack_from('<I', data, 12)
        count, x, y, cx, cy = struct.unpack_from('<Hhhhh', data, 16)
        pos = 26
    else:
        style, _, count, x, y, cx, cy = struct.unpack_from('<IIHhhhh', data, 0)
        pos = 18
    _, pos = wide_or_ordinal(data, pos)      # menu
    _, pos = wide_or_ordinal(data, pos)      # window class
    title, pos = wide_or_ordinal(data, pos)
    if style & 0x40:                         # DS_SETFONT
        pos += 6 if extended else 2
        _, pos = wide_or_ordinal(data, pos)
    controls = []
    for _ in range(count):
        pos = (pos + 3) & ~3
        if extended:
            _, _, cstyle, cx0, cy0, ccx, ccy, cid = struct.unpack_from('<IIIhhhhI', data, pos)
            pos += 24
        else:
            cstyle, _, cx0, cy0, ccx, ccy, cid = struct.unpack_from('<IIhhhhH', data, pos)
            pos += 18
        cls, pos = wide_or_ordinal(data, pos)
        text, pos = wide_or_ordinal(data, pos)
        extra = struct.unpack_from('<H', data, pos)[0]
        pos += 2 + extra
        controls.append({'id': cid, 'class': cls, 'style': cstyle, 'text': text,
                         'rect': [cx0, cy0, ccx, ccy]})
    return {'extended': extended, 'style': style, 'title': title,
            'size': [cx, cy], 'controls': controls}


def extract(exe, out):
    data = Path(exe).read_bytes()
    bmp = Path(out) / 'bmp'
    bmp.mkdir(parents=True, exist_ok=True)
    dialogs = {}
    bitmaps = 0
    for item in PE(data).resources():
        blob = data[item['offset']:item['offset'] + item['size']]
        if item['type'] == 'DIB' and blob[:2] == b'BM':
            (bmp / f"{item['name']}.bmp").write_bytes(blob)
            bitmaps += 1
        elif item['type'] == RT_DIALOG:
            dialogs[str(item['name'])] = parse_dialog(blob)
    (Path(out) / 'dialogs.json').write_text(json.dumps(dialogs, indent=1))
    return bitmaps, len(dialogs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, help='A10Cuba.exe')
    parser.add_argument('--out', type=Path, default=Path('data/ui'))
    args = parser.parse_args()
    try:
        bitmaps, dialogs = extract(args.exe, args.out)
    except (OSError, PEError, struct.error, UnicodeError) as exc:
        print(f'{parser.prog}: {exc}', file=sys.stderr)
        return 1
    print(f'{bitmaps} bitmaps, {dialogs} dialogs -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
