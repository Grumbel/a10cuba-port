#!/usr/bin/env python3
"""Check SDL rendering of a padded indexed BMP using a synthetic palette."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main():
    viewer = sys.argv[1]
    # BMP scanlines are bottom-up, with each three-byte row padded to four.
    palette = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    entries = b''.join(bytes((b, g, r, 0)) for r, g, b in palette)
    pixels = bytes((3, 2, 1, 0, 1, 2, 3, 0))
    offset = 14 + 40 + len(entries)
    header = struct.pack('<2sIHHI', b'BM', offset + len(pixels), 0, 0, offset)
    info = struct.pack('<IiiHHIIiiII', 40, 3, 2, 1, 8, 0, len(pixels), 0, 0, len(palette), 0)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'input.bmp'
        snapshot = Path(directory) / 'rendered.bmp'
        source.write_bytes(header + info + entries + pixels)
        env = os.environ.copy()
        env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software')
        subprocess.run([viewer, str(source), '--snapshot', str(snapshot)], env=env, check=True)
        data = snapshot.read_bytes()
        pixel_offset = struct.unpack_from('<I', data, 10)[0]
        width, height, _, bits, compression = struct.unpack_from('<iiHHI', data, 18)
        if (width, height, bits, compression) != (3, 2, 24, 0):
            raise AssertionError(f'unexpected rendered BMP layout: {(width, height, bits, compression)}')
        stride = ((width * 3 + 3) // 4) * 4
        rows = []
        for y in reversed(range(height)):
            row = data[pixel_offset + y * stride:pixel_offset + y * stride + width * 3]
            rows.append([tuple(reversed(row[x:x + 3])) for x in range(0, len(row), 3)])
        if rows != [[palette[1], palette[2], palette[3]], [palette[3], palette[2], palette[1]]]:
            raise AssertionError(f'unexpected rendered pixels: {rows!r}')


if __name__ == '__main__':
    main()
