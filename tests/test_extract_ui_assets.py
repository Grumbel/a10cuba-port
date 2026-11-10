import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import extract_ui_assets as ui  # noqa: E402


def wide(text):
    return text.encode('utf-16le') + b'\0\0'


class DialogParseTest(unittest.TestCase):
    def test_plain_dialog(self):
        data = struct.pack('<IIHhhhh', 0, 0, 1, 1, 2, 30, 40) + b'\0\0' * 2 + wide('T')
        data += b'\0' * (-len(data) % 4)
        data += struct.pack('<IIhhhhH', 0x50010000, 0, 5, 6, 7, 8, 99)
        data += b'\xff\xff\x80\x00' + wide('OK') + b'\0\0'
        dialog = ui.parse_dialog(data)
        self.assertEqual(dialog['size'], [30, 40])
        self.assertEqual(dialog['title'], 'T')
        self.assertEqual(dialog['controls'][0]['id'], 99)
        self.assertEqual(dialog['controls'][0]['class'], 128)
        self.assertEqual(dialog['controls'][0]['text'], 'OK')
        self.assertEqual(dialog['controls'][0]['rect'], [5, 6, 7, 8])

    def test_extended_dialog_with_font(self):
        data = struct.pack('<HHIIIHhhhh', 1, 0xffff, 0, 0, 0x40, 1, 0, 0, 50, 60)
        data += b'\0\0' * 2 + wide('') + struct.pack('<HHBB', 8, 400, 0, 1) + wide('f')
        data += b'\0' * (-len(data) % 4)
        data += struct.pack('<IIIhhhhI', 0, 0, 0x5001000b, 1, 2, 3, 4, 1136)
        data += b'\xff\xff\x80\x00' + wide('Q') + b'\0\0'
        dialog = ui.parse_dialog(data)
        self.assertTrue(dialog['extended'])
        self.assertEqual(dialog['size'], [50, 60])
        self.assertEqual(dialog['controls'][0]['id'], 1136)
        self.assertEqual(dialog['controls'][0]['text'], 'Q')


if __name__ == '__main__':
    unittest.main()
