/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <string.h>

#include "pe32_sys.h"

int main(void)
{
    pe32_u32 key = 7, disposition = 0, address = 0;
    char title[32];
    assert(pe32_win32_reg_open_key_ex_a(0x80000001u, "Software", 0, 1, &key) == 2);
    assert(key == 0);
    assert(pe32_win32_reg_query_value_ex_a(0, "x", 0, 0, 0, 0) == 2);
    assert(pe32_win32_reg_create_key_ex_a(0x80000001u, "Software\\Test", 0, 0, 0, 0,
                                          0, &key, &disposition) == 0);
    assert(key && disposition == 1);
    {
        unsigned char data[8];
        pe32_u32 size = sizeof data, type = 0, again = 0, key2 = 0;
        assert(pe32_win32_reg_set_value_ex_a(key, "v", 0, 4,
                                             (const unsigned char *)"\x2a\0\0\0",
                                             4) == 0);
        assert(pe32_win32_reg_query_value_ex_a(key, "V", 0, &type, data, &size) == 0);
        assert(type == 4 && size == 4 && data[0] == 42);
        size = 2;
        assert(pe32_win32_reg_query_value_ex_a(key, "v", 0, &type, data, &size) == 234);
        assert(size == 4);
        assert(pe32_win32_reg_query_value_ex_a(key, "missing", 0, 0, 0, 0) == 2);
        assert(pe32_win32_reg_create_key_ex_a(0x80000001u, "software\\test", 0, 0, 0,
                                              0, 0, &key2, &again) == 0);
        assert(key2 == key && again == 2);
        /* Seeded sound scheme. */
        assert(pe32_win32_reg_open_key_ex_a(
                   0x80000001u, "AppEvents\\Schemes\\Apps\\A10Cuba\\A-10Engine\\.Current",
                   0, 1, &key2) == 0);
        size = sizeof data;
        assert(pe32_win32_reg_query_value_ex_a(key2, 0, 0, &type, data, &size) == 234);
        {
            char path[64];
            size = sizeof path;
            assert(pe32_win32_reg_query_value_ex_a(key2, 0, 0, &type,
                                                   (unsigned char *)path, &size) == 0);
            assert(type == 1 && strcmp(path, "Media\\EngineA-10.wav") == 0);
        }
    }
    assert(pe32_win32_reg_close_key(key) == 0);
    assert(pe32_win32_get_open_file_name_a(0) == 0);
    assert(pe32_win32_get_file_title_a("C:\\dir\\save.dat", title, 32) == 0);
    assert(strcmp(title, "save.dat") == 0);
    assert(pe32_win32_get_file_title_a("C:\\dir\\save.dat", title, 4) == 9);
    assert(pe32_sys_resolve("USER32.dll", "CreateWindowExA", &address) ==
           PE32_LINK_MISSING);
    return 0;
}
