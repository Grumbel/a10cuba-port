/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <string.h>

#include "pe32_res.h"

static pe32_u8 image[0x400];

static void put16(unsigned off, unsigned v) { image[off] = v; image[off + 1] = v >> 8; }
static void put32(unsigned off, unsigned v) { put16(off, v & 0xffff); put16(off + 2, v >> 16); }

/* Layout: PE hdr @0x20; .rsrc @0x100: type dir -> name dir -> lang dir ->
 * data entry @0x1a0, data @0x200 ("HELLO"). Types: RT_RCDATA(10), "WAVE". */
int main(void)
{
    struct pe32_module m;
    pe32_u32 res, data;
    memset(image, 0, sizeof image);
    put32(0x3c, 0x20);
    put32(0x20 + 0x78 + 16, 0x100);
    put32(0x20 + 0x7c + 16, 0x100);
    /* Type dir: 1 named ("WAVE"), 1 id (10). */
    put16(0x100 + 12, 1); put16(0x100 + 14, 1);
    put32(0x110, 0x80000000u | 0x60); put32(0x114, 0x80000000u | 0x20);
    put32(0x118, 10);                 put32(0x11c, 0x80000000u | 0x40);
    /* Name dir @0x120 (id 7), name dir @0x140 (id 1 under type 10). */
    put16(0x120 + 14, 1); put32(0x130, 7); put32(0x134, 0x80000000u | 0x80);
    put16(0x140 + 14, 1); put32(0x150, 7); put32(0x154, 0x80000000u | 0x80);
    /* Named "WAVE" -> dir @0x160 (id 7 -> lang dir @0x80). */
    put16(0x160 + 14, 1); put32(0x170, 7); put32(0x174, 0x80000000u | 0x80);
    /* The type name string at 0x100+0x60 collides; place it at 0x1c0. */
    put32(0x110, 0x80000000u | 0xc0);
    put16(0x1c0, 4);
    put16(0x1c2, 'W'); put16(0x1c4, 'A'); put16(0x1c6, 'V'); put16(0x1c8, 'E');
    /* Lang dir @0x180 (0x100+0x80): one id entry -> data entry @0x1a0. */
    put16(0x180 + 14, 1); put32(0x190, 0x409); put32(0x194, 0xa0);
    put32(0x1a0, 0x200); put32(0x1a4, 5);
    memcpy(image + 0x200, "HELLO", 5);

    m.image = image; m.size = sizeof image; m.base = 0x10000;
    m.entry_rva = m.export_rva = m.export_size = m.import_rva = 0;
    m.import_size = m.tls_rva = m.tls_size = 0; m.path = "x.exe";
    pe32_win32_initialize(&m, 1);

    res = pe32_win32_find_resource_a(0, (const char *)7, (const char *)10);
    assert(res);
    assert(pe32_win32_sizeof_resource(0, res) == 5);
    data = pe32_win32_load_resource(0, res);
    assert(data == 0x10200 && memcmp(image + 0x200, "HELLO", 5) == 0);
    assert(pe32_win32_lock_resource(data) == data);
    assert(pe32_win32_find_resource_a(0, "#7", "#10") == res);
    assert(pe32_win32_find_resource_a(0, (const char *)8, (const char *)10) == 0);
    assert(pe32_win32_get_last_error() == 1814);
    assert(pe32_win32_find_resource_a(0, (const char *)7, (const char *)3) == 0);
    assert(pe32_win32_get_last_error() == 1813);
    assert(pe32_win32_find_resource_a(0, (const char *)7, "wave") != 0);
    assert(pe32_win32_load_resource(0, 5) == 0);
    return 0;
}
