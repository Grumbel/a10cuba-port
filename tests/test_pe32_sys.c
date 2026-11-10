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
    assert(pe32_win32_reg_create_key_ex_a(0, "x", 0, 0, 0, 0, 0, &key,
                                          &disposition) == 0);
    assert(key && disposition == 1);
    assert(pe32_win32_reg_set_value_ex_a(key, "v", 0, 1, 0, 0) == 0);
    assert(pe32_win32_reg_close_key(key) == 0);
    assert(pe32_win32_get_open_file_name_a(0) == 0);
    assert(pe32_win32_get_file_title_a("C:\\dir\\save.dat", title, 32) == 0);
    assert(strcmp(title, "save.dat") == 0);
    assert(pe32_win32_get_file_title_a("C:\\dir\\save.dat", title, 4) == 9);
    assert(pe32_sys_resolve("USER32.dll", "CreateWindowExA", &address) ==
           PE32_LINK_MISSING);
    return 0;
}
