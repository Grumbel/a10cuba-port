/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <string.h>

#include "pe32_misc.h"

int main(void)
{
    struct pe32_filetime ft, local;
    struct pe32_system_time st;
    char name[16];
    pe32_u32 a, b;
    unsigned char of[PE32_OFSTRUCT_SIZE];

    /* 2000-01-01 00:00:00.5 UTC, a Saturday. */
    ft.low = (pe32_u32)(125911584005000000ULL & 0xffffffffu);
    ft.high = (pe32_u32)(125911584005000000ULL >> 32);
    assert(pe32_win32_file_time_to_system_time(&ft, &st));
    assert(st.year == 2000 && st.month == 1 && st.day == 1);
    assert(st.hour == 0 && st.minute == 0 && st.second == 0);
    assert(st.milliseconds == 500 && st.day_of_week == 6);
    /* 2024-02-29 13:45:10 (leap day): 133540719100000000 ticks. */
    ft.low = (pe32_u32)(133536879100000000ULL & 0xffffffffu);
    ft.high = (pe32_u32)(133536879100000000ULL >> 32);
    assert(pe32_win32_file_time_to_system_time(&ft, &st));
    assert(st.year == 2024 && st.month == 2 && st.day == 29);
    assert(st.hour == 13 && st.minute == 45 && st.second == 10);
    assert(st.day_of_week == 4);
    assert(pe32_win32_file_time_to_local_file_time(&ft, &local));
    assert(local.low == ft.low && local.high == ft.high);
    ft.high = 0x80000000u;
    assert(!pe32_win32_file_time_to_system_time(&ft, &st));

    a = pe32_win32_global_add_atom_a("Hello");
    assert(a >= 0xc000);
    assert(pe32_win32_global_add_atom_a("HELLO") == a);
    assert(pe32_win32_global_get_atom_name_a(a, name, sizeof name) == 5);
    assert(strcmp(name, "Hello") == 0);
    assert(pe32_win32_global_delete_atom(a) == 0);
    assert(pe32_win32_global_delete_atom(a) == 0);
    assert(pe32_win32_global_delete_atom(a) == a);
    b = pe32_win32_global_add_atom_a("Other");
    assert(b >= 0xc000);

    assert(pe32_win32_open_file("missing.dat", of, 0x4000) == 0xffffffffu);
    assert(pe32_win32_get_volume_information_a(0, name, 16, &a, &b, 0, 0, 0));
    assert(strcmp(name, "A10CUBA") == 0);
    return 0;
}
