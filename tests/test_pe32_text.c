/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Host-native tests of the freestanding KERNEL32 text and locale shims. */
#include "pe32_text.h"
#include "pe32_win32.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void test_lstrcmp(void)
{
    assert(pe32_win32_lstrcmp_a("abc", "abc") == 0);
    assert(pe32_win32_lstrcmp_a("abc", "abd") < 0);
    assert(pe32_win32_lstrcmp_a("abd", "abc") > 0);
    assert(pe32_win32_lstrcmp_a("ab", "abc") < 0);
    assert(pe32_win32_lstrcmp_a("B", "a") < 0);
    assert(pe32_win32_lstrcmpi_a("ABC", "abc") == 0);
    assert(pe32_win32_lstrcmpi_a("a", "B") < 0);
    assert(pe32_win32_lstrcmpi_a("C", "b") > 0);
}

static void test_mul_div(void)
{
    assert(pe32_win32_mul_div(3, 4, 2) == 6);
    assert(pe32_win32_mul_div(5, 1, 2) == 3);
    assert(pe32_win32_mul_div(-5, 1, 2) == -3);
    assert(pe32_win32_mul_div(5, -1, 2) == -3);
    assert(pe32_win32_mul_div(-5, -1, 2) == 3);
    assert(pe32_win32_mul_div(1000000, 1000000, 1000000) == 1000000);
    assert(pe32_win32_mul_div(65536, 65536, 65536) == 65536);
    assert(pe32_win32_mul_div(INT_MIN, 1, 1) == INT_MIN);
    assert(pe32_win32_mul_div(INT_MAX, 2, 2) == INT_MAX);
    assert(pe32_win32_mul_div(1, 1, 0) == -1);
    assert(pe32_win32_mul_div(0x40000000, 4, 1) == -1);
    assert(pe32_win32_mul_div(INT_MAX, 2, 1) == -1);
}

static void test_bad_ptr_and_versions(void)
{
    char byte = 0;
    assert(pe32_win32_is_bad_read_ptr(0, 4) == 1);
    assert(pe32_win32_is_bad_read_ptr(&byte, 4) == 0);
    assert(pe32_win32_is_bad_read_ptr(&byte, 0) == 0);
    assert(pe32_win32_is_bad_write_ptr(0, 4) == 1);
    assert(pe32_win32_is_bad_write_ptr(&byte, 1) == 0);
    assert(pe32_win32_is_bad_code_ptr(0) == 1);
    assert(pe32_win32_is_bad_code_ptr(&byte) == 0);
    assert(pe32_win32_get_acp() == 1252);
    assert(pe32_win32_get_oemcp() == 437);
    assert(pe32_win32_get_version() == 0xC0000004u);
}

static void test_cp_info(void)
{
    pe32_u8 info[18];
    unsigned i;
    for (i = 0; i < sizeof(info); ++i) {
        info[i] = 0xaa;
    }
    pe32_win32_set_last_error(0);
    assert(pe32_win32_get_cp_info(1252, info) == 1);
    assert(info[0] == 1 && info[1] == 0 && info[2] == 0 && info[3] == 0);
    assert(info[4] == '?' && info[5] == 0);
    for (i = 6; i < sizeof(info); ++i) {
        assert(info[i] == 0);
    }
    assert(pe32_win32_get_cp_info(437, info) == 1);
    assert(pe32_win32_get_cp_info(0, info) == 1);

    pe32_win32_set_last_error(0);
    assert(pe32_win32_get_cp_info(65001, info) == 0);
    assert(pe32_win32_get_last_error() == 87);
    assert(pe32_win32_get_cp_info(1252, 0) == 0);
}

static void test_multi_byte_to_wide(void)
{
    static const char text[] = "abc";
    static const char latin1[] = {'\xe9', 'x'};
    pe32_u16 wide[8];

    /* -1 includes the NUL; zero destination size queries the length. */
    assert(pe32_win32_multi_byte_to_wide_char(1252, 0, text, -1, 0, 0) == 4);
    assert(pe32_win32_multi_byte_to_wide_char(1252, 0, text, 3, 0, 0) == 3);
    assert(pe32_win32_multi_byte_to_wide_char(1252, 0, text, -1, wide, 4) == 4);
    assert(wide[0] == 'a' && wide[1] == 'b' && wide[2] == 'c' && wide[3] == 0);
    assert(pe32_win32_multi_byte_to_wide_char(1252, 0, latin1, 2, wide, 8) == 2);
    assert(wide[0] == 0xe9 && wide[1] == 'x');

    pe32_win32_set_last_error(0);
    assert(pe32_win32_multi_byte_to_wide_char(1252, 0, text, -1, wide, 3) == 0);
    assert(pe32_win32_get_last_error() == 122);

    pe32_win32_set_last_error(0);
    assert(pe32_win32_multi_byte_to_wide_char(65001, 0, text, -1, wide, 8) == 0);
    assert(pe32_win32_get_last_error() == 87);
}

static void test_wide_to_multi_byte(void)
{
    static const pe32_u16 wide[] = {'a', 0xe9, 0x4e2d, 0};
    char out[8];
    int used = -1;

    assert(pe32_win32_wide_char_to_multi_byte(1252, 0, wide, -1, 0, 0, 0,
                                              &used) == 4);
    assert(used == 1);
    assert(pe32_win32_wide_char_to_multi_byte(1252, 0, wide, 2, 0, 0, 0,
                                              &used) == 2);
    assert(used == 0);

    used = -1;
    assert(pe32_win32_wide_char_to_multi_byte(1252, 0, wide, 3, out, 8, 0,
                                              &used) == 3);
    assert(out[0] == 'a' && (pe32_u8)out[1] == 0xe9 && out[2] == '?');
    assert(used == 1);

    assert(pe32_win32_wide_char_to_multi_byte(1252, 0, wide, 3, out, 8, "#",
                                              0) == 3);
    assert(out[2] == '#');

    pe32_win32_set_last_error(0);
    assert(pe32_win32_wide_char_to_multi_byte(1252, 0, wide, -1, out, 2, 0, 0)
           == 0);
    assert(pe32_win32_get_last_error() == 122);
}

static void test_string_type(void)
{
    pe32_u16 types[4];

    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, " ", 1, types));
    assert(types[0] == (PE32_C1_SPACE | PE32_C1_BLANK));
    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, "A", 1, types));
    assert(types[0] == (PE32_C1_UPPER | PE32_C1_ALPHA | PE32_C1_XDIGIT));
    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, "z", 1, types));
    assert(types[0] == (PE32_C1_LOWER | PE32_C1_ALPHA));
    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, "5", 1, types));
    assert(types[0] == (PE32_C1_DIGIT | PE32_C1_XDIGIT));
    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, "!", 1, types));
    assert(types[0] == PE32_C1_PUNCT);
    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, "\n", 1, types));
    assert(types[0] == (PE32_C1_CNTRL | PE32_C1_SPACE));
    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, "\xe9", 1, types));
    assert(types[0] == 0);

    /* -1 covers the NUL, which is a control character. */
    assert(pe32_win32_get_string_type_a(0x409, PE32_CT_CTYPE1, "A", -1, types));
    assert(types[1] == PE32_C1_CNTRL);

    assert(!pe32_win32_get_string_type_a(0x409, 2, "A", 1, types));
    assert(pe32_win32_get_last_error() == 87);

    {
        static const pe32_u16 wide_a[] = {'A', 0};
        assert(pe32_win32_get_string_type_w(PE32_CT_CTYPE1, wide_a, 1, types));
        assert(types[0] == (PE32_C1_UPPER | PE32_C1_ALPHA | PE32_C1_XDIGIT));
        assert(pe32_win32_get_string_type_w(PE32_CT_CTYPE1, wide_a, -1, types));
        assert(types[1] == PE32_C1_CNTRL);
    }
}

static void test_lc_map_string(void)
{
    static const char mixed[] = "AbC1";
    static const pe32_u16 wide_mixed[] = {'A', 'b', 'C', '1', 0};
    char out[8];
    pe32_u16 wide_out[8];

    assert(pe32_win32_lc_map_string_a(0x409, PE32_LCMAP_LOWERCASE, mixed, 4,
                                      0, 0) == 4);
    assert(pe32_win32_lc_map_string_a(0x409, PE32_LCMAP_LOWERCASE, mixed, 4,
                                      out, 4) == 4);
    assert(out[0] == 'a' && out[1] == 'b' && out[2] == 'c' && out[3] == '1');
    assert(pe32_win32_lc_map_string_a(0x409, PE32_LCMAP_UPPERCASE, mixed, -1,
                                      out, 8) == 5);
    assert(out[0] == 'A' && out[1] == 'B' && out[2] == 'C' && out[4] == 0);
    assert(pe32_win32_lc_map_string_a(0x409, 0, mixed, 4, out, 4) == 4);
    assert(out[1] == 'b');

    pe32_win32_set_last_error(0);
    assert(pe32_win32_lc_map_string_a(0x409, PE32_LCMAP_LOWERCASE, mixed, -1,
                                      out, 3) == 0);
    assert(pe32_win32_get_last_error() == 122);

    pe32_win32_set_last_error(0);
    assert(pe32_win32_lc_map_string_a(0x409, PE32_LCMAP_LOWERCASE |
                                              PE32_LCMAP_UPPERCASE,
                                      mixed, 4, out, 8) == 0);
    assert(pe32_win32_get_last_error() == 1004);

    assert(pe32_win32_lc_map_string_w(0x409, PE32_LCMAP_UPPERCASE, wide_mixed,
                                      -1, 0, 0) == 5);
    assert(pe32_win32_lc_map_string_w(0x409, PE32_LCMAP_UPPERCASE, wide_mixed,
                                      -1, wide_out, 8) == 5);
    assert(wide_out[0] == 'A' && wide_out[1] == 'B' && wide_out[2] == 'C');
    assert(pe32_win32_lc_map_string_w(0x409, PE32_LCMAP_LOWERCASE, wide_mixed,
                                      4, wide_out, 8) == 4);
    assert(wide_out[0] == 'a' && wide_out[2] == 'c' && wide_out[3] == '1');
}

static void test_compare_string(void)
{
    static const char abc[] = "abc";
    static const char abd[] = "abd";
    static const char upper[] = "ABC";
    static const pe32_u16 wide_abc[] = {'a', 'b', 'c', 0};
    static const pe32_u16 wide_abd[] = {'a', 'b', 'd', 0};
    static const pe32_u16 wide_upper[] = {'A', 'B', 'C', 0};

    assert(pe32_win32_compare_string_a(0x409, 0, abc, 3, abc, 3) == 2);
    assert(pe32_win32_compare_string_a(0x409, 0, abc, 3, abd, 3) ==
           PE32_CSTR_LESS_THAN);
    assert(pe32_win32_compare_string_a(0x409, 0, abd, 3, abc, 3) ==
           PE32_CSTR_GREATER_THAN);
    assert(pe32_win32_compare_string_a(0x409, 0, abc, -1, abc, -1) == 2);
    assert(pe32_win32_compare_string_a(0x409, 0, "ab", -1, abc, -1) ==
           PE32_CSTR_LESS_THAN);
    assert(pe32_win32_compare_string_a(0x409, 0, abc, 2, abc, 3) ==
           PE32_CSTR_LESS_THAN);
    assert(pe32_win32_compare_string_a(0x409, 0, upper, 3, abc, 3) ==
           PE32_CSTR_LESS_THAN);
    assert(pe32_win32_compare_string_a(0x409, PE32_NORM_IGNORECASE, upper, -1,
                                       abc, -1) == 2);
    assert(pe32_win32_compare_string_a(0x409, PE32_NORM_IGNORECASE, upper, 3,
                                       abd, 3) == PE32_CSTR_LESS_THAN);

    pe32_win32_set_last_error(0);
    assert(pe32_win32_compare_string_a(0x409, 0, 0, -1, abc, -1) == 0);
    assert(pe32_win32_get_last_error() == 87);

    assert(pe32_win32_compare_string_w(0x409, 0, wide_abc, 3, wide_abc, 3) == 2);
    assert(pe32_win32_compare_string_w(0x409, 0, wide_abc, 3, wide_abd, 3) ==
           PE32_CSTR_LESS_THAN);
    assert(pe32_win32_compare_string_w(0x409, 0, wide_upper, -1, wide_abc,
                                       -1) == PE32_CSTR_LESS_THAN);
    assert(pe32_win32_compare_string_w(0x409, PE32_NORM_IGNORECASE, wide_upper,
                                       -1, wide_abc, -1) == 2);
}

static void test_locale_info(void)
{
    char out[8];
    pe32_u16 wide_out[8];

    assert(pe32_win32_get_locale_info_a(0x409, 0x0e, 0, 0) == 2);
    assert(pe32_win32_get_locale_info_a(0x409, 0x0e, out, 2) == 2);
    assert(out[0] == '.' && out[1] == 0);
    assert(pe32_win32_get_locale_info_a(0x409, 0x0f, out, 2) == 2);
    assert(out[0] == ',');
    assert(pe32_win32_get_locale_info_a(0x409, 0x01, out, 5) == 5);
    assert(out[0] == '0' && out[1] == '4' && out[2] == '0' && out[3] == '9');

    pe32_win32_set_last_error(0);
    assert(pe32_win32_get_locale_info_a(0x409, 0x0e, out, 1) == 0);
    assert(pe32_win32_get_last_error() == 122);

    pe32_win32_set_last_error(0);
    assert(pe32_win32_get_locale_info_a(0x409, 0x1234, out, 8) == 0);
    assert(pe32_win32_get_last_error() == 1004);

    assert(pe32_win32_get_locale_info_w(0x409, 0x0e, 0, 0) == 2);
    assert(pe32_win32_get_locale_info_w(0x409, 0x0e, wide_out, 2) == 2);
    assert(wide_out[0] == '.' && wide_out[1] == 0);
    assert(pe32_win32_get_locale_info_w(0x409, 0x0f, wide_out, 2) == 2);
    assert(wide_out[0] == ',');
}

int main(void)
{
    test_lstrcmp();
    test_mul_div();
    test_bad_ptr_and_versions();
    test_cp_info();
    test_multi_byte_to_wide();
    test_wide_to_multi_byte();
    test_string_type();
    test_lc_map_string();
    test_compare_string();
    test_locale_info();
    puts("test_pe32_text: all tests passed");
    return 0;
}
