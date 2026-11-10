/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Freestanding: no libc and no allocation. Avoid 64-bit division here, since
 * i386 would then need libgcc; divide_u64 uses shifts instead. */

#include "pe32_text.h"
#include "pe32_win32.h"

#define PE32_CP_ACP 0u
#define PE32_CP_OEMCP 1u
#define PE32_CP_THREAD_ACP 3u
#define PE32_CP_1252 1252u
#define PE32_CP_437 437u

#define PE32_LCTYPE_FLAG_NOUSEROVERRIDE 0x80000000u
#define PE32_LCTYPE_FLAG_USE_CP_ACP 0x40000000u

enum {
    PE32_ERROR_INVALID_PARAMETER = 87,
    PE32_ERROR_INSUFFICIENT_BUFFER = 122,
    PE32_ERROR_INVALID_FLAGS = 1004,
    PE32_CPINFO_SIZE = 18
};

static pe32_u32 string_length_a(const char *string)
{
    pe32_u32 length = 0;
    while (string[length]) {
        ++length;
    }
    return length;
}

static pe32_u32 string_length_w(const pe32_u16 *string)
{
    pe32_u32 length = 0;
    while (string[length]) {
        ++length;
    }
    return length;
}

static int code_page_supported(pe32_u32 code_page)
{
    return code_page == PE32_CP_ACP || code_page == PE32_CP_OEMCP ||
           code_page == PE32_CP_THREAD_ACP || code_page == PE32_CP_1252 ||
           code_page == PE32_CP_437;
}

static pe32_u32 ascii_lower(pe32_u32 c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32u : c;
}

static pe32_u32 ascii_upper(pe32_u32 c)
{
    return (c >= 'a' && c <= 'z') ? c - 32u : c;
}

static pe32_u32 magnitude_of(int value)
{
    return value < 0 ? 0u - (pe32_u32)value : (pe32_u32)value;
}

/* Shift-subtract long division: keeps 64-bit division out of libgcc. */
static unsigned long long divide_u64(unsigned long long numerator,
                                     pe32_u32 divisor)
{
    unsigned long long quotient = 0;
    unsigned long long remainder = 0;
    int bit;
    for (bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((numerator >> bit) & 1u);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1ull << bit;
        }
    }
    return quotient;
}

int PE32_WINAPI pe32_win32_lstrcmp_a(const char *string1, const char *string2)
{
    pe32_u32 a, b;
    do {
        a = (pe32_u8)*string1++;
        b = (pe32_u8)*string2++;
    } while (a && a == b);
    return (int)a - (int)b;
}

int PE32_WINAPI pe32_win32_lstrcmpi_a(const char *string1, const char *string2)
{
    pe32_u32 a, b;
    do {
        a = ascii_lower((pe32_u8)*string1++);
        b = ascii_lower((pe32_u8)*string2++);
    } while (a && a == b);
    return (int)a - (int)b;
}

int PE32_WINAPI pe32_win32_mul_div(int number, int numerator, int denominator)
{
    pe32_u32 divisor;
    int negative;
    unsigned long long quotient;

    if (denominator == 0) {
        return -1;
    }
    negative = (number < 0) ^ (numerator < 0) ^ (denominator < 0);
    divisor = magnitude_of(denominator);
    /* Round half away from zero: add half the divisor before dividing. */
    quotient = (unsigned long long)magnitude_of(number) *
               magnitude_of(numerator);
    quotient = divide_u64(quotient + divisor / 2u, divisor);
    if (quotient > (negative ? 0x80000000ull : 0x7fffffffull)) {
        return -1;
    }
    if (negative) {
        return (int)(0u - (pe32_u32)quotient);
    }
    return (int)(pe32_u32)quotient;
}

int PE32_WINAPI pe32_win32_is_bad_read_ptr(const void *pointer, pe32_u32 size)
{
    if (size == 0) {
        return 0;
    }
    return pointer == 0;
}

int PE32_WINAPI pe32_win32_is_bad_write_ptr(void *pointer, pe32_u32 size)
{
    return pe32_win32_is_bad_read_ptr(pointer, size);
}

int PE32_WINAPI pe32_win32_is_bad_code_ptr(const void *pointer)
{
    return pointer == 0;
}

pe32_u32 PE32_WINAPI pe32_win32_get_acp(void)
{
    return PE32_CP_1252;
}

pe32_u32 PE32_WINAPI pe32_win32_get_oemcp(void)
{
    return PE32_CP_437;
}

int PE32_WINAPI pe32_win32_get_cp_info(pe32_u32 code_page, pe32_u8 *cp_info)
{
    pe32_u32 i;
    if (!code_page_supported(code_page) || !cp_info) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* UINT MaxCharSize, little-endian byte stores (guest buffer may be
     * unaligned). */
    cp_info[0] = 1;
    cp_info[1] = 0;
    cp_info[2] = 0;
    cp_info[3] = 0;
    cp_info[4] = '?';
    cp_info[5] = 0;
    for (i = 6; i < PE32_CPINFO_SIZE; ++i) {
        cp_info[i] = 0;
    }
    return 1;
}

pe32_u32 PE32_WINAPI pe32_win32_get_version(void)
{
    return 0xC0000004u;
}

int PE32_WINAPI pe32_win32_multi_byte_to_wide_char(pe32_u32 code_page,
                                                  pe32_u32 flags,
                                                  const char *source,
                                                  int source_length,
                                                  pe32_u16 *dest,
                                                  int dest_length)
{
    pe32_u32 count, i;
    (void)flags; /* No multibyte sequences exist in single-byte Latin-1. */

    if (!code_page_supported(code_page) || !source || source_length == 0 ||
        source_length < -1 || dest_length < 0 ||
        (dest_length > 0 && !dest)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* -1 means NUL-terminated: the terminating NUL is converted too. */
    count = source_length == -1 ? string_length_a(source) + 1u
                                : (pe32_u32)source_length;
    if (dest_length == 0) {
        return (int)count;
    }
    if (count > (pe32_u32)dest_length) {
        pe32_win32_set_last_error(PE32_ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        dest[i] = (pe32_u16)(pe32_u8)source[i];
    }
    return (int)count;
}

int PE32_WINAPI pe32_win32_wide_char_to_multi_byte(pe32_u32 code_page,
                                                  pe32_u32 flags,
                                                  const pe32_u16 *source,
                                                  int source_length,
                                                  char *dest,
                                                  int dest_length,
                                                  const char *default_char,
                                                  int *used_default)
{
    pe32_u32 count, i;
    pe32_u8 fallback = default_char ? (pe32_u8)*default_char : (pe32_u8)'?';
    int used = 0;
    (void)flags;

    if (!code_page_supported(code_page) || !source || source_length == 0 ||
        source_length < -1 || dest_length < 0 ||
        (dest_length > 0 && !dest)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    count = source_length == -1 ? string_length_w(source) + 1u
                                : (pe32_u32)source_length;
    /* Anything above U+00FF has no Latin-1 byte and becomes the default. */
    for (i = 0; i < count; ++i) {
        if (source[i] > 0xffu) {
            used = 1;
        }
    }
    if (used_default) {
        *used_default = used;
    }
    if (dest_length == 0) {
        return (int)count;
    }
    if (count > (pe32_u32)dest_length) {
        pe32_win32_set_last_error(PE32_ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        dest[i] = (char)(source[i] > 0xffu ? fallback : (pe32_u8)source[i]);
    }
    return (int)count;
}

/* Character classes for ASCII only: bytes and code units >= 0x80 get 0. */
static pe32_u16 ascii_char_type(pe32_u32 c)
{
    pe32_u16 type = 0;
    int alpha, digit;
    if (c >= 0x80u) {
        return 0;
    }
    alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    digit = (c >= '0' && c <= '9');
    if (c < 0x20u || c == 0x7fu) {
        type |= PE32_C1_CNTRL;
    }
    if (c == 0x09u || c == 0x20u) {
        type |= PE32_C1_BLANK;
    }
    if ((c >= 0x09u && c <= 0x0du) || c == 0x20u) {
        type |= PE32_C1_SPACE;
    }
    if (c >= 'A' && c <= 'Z') {
        type |= PE32_C1_UPPER;
    }
    if (c >= 'a' && c <= 'z') {
        type |= PE32_C1_LOWER;
    }
    if (alpha) {
        type |= PE32_C1_ALPHA;
    }
    if (digit) {
        type |= PE32_C1_DIGIT;
    }
    if (digit || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
        type |= PE32_C1_XDIGIT;
    }
    if (c >= 0x21u && c <= 0x7eu && !alpha && !digit) {
        type |= PE32_C1_PUNCT;
    }
    return type;
}

int PE32_WINAPI pe32_win32_get_string_type_a(pe32_u32 locale,
                                            pe32_u32 info_type,
                                            const char *source, int count,
                                            pe32_u16 *char_types)
{
    pe32_u32 i, length;
    (void)locale;
    if (info_type != PE32_CT_CTYPE1 || !source || !char_types ||
        count < -1) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* -1 includes the NUL, which is classified as CNTRL, as on Windows. */
    length = count == -1 ? string_length_a(source) + 1u : (pe32_u32)count;
    for (i = 0; i < length; ++i) {
        char_types[i] = ascii_char_type((pe32_u8)source[i]);
    }
    return 1;
}

int PE32_WINAPI pe32_win32_get_string_type_w(pe32_u32 info_type,
                                            const pe32_u16 *source, int count,
                                            pe32_u16 *char_types)
{
    pe32_u32 i, length;
    if (info_type != PE32_CT_CTYPE1 || !source || !char_types ||
        count < -1) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    length = count == -1 ? string_length_w(source) + 1u : (pe32_u32)count;
    for (i = 0; i < length; ++i) {
        char_types[i] = ascii_char_type(source[i]);
    }
    return 1;
}

/* LCMAP_LOWERCASE and LCMAP_UPPERCASE together are rejected. Other flags
 * (for example LCMAP_SORTKEY) are ignored and the text is copied. */
static int lc_map_flags_valid(pe32_u32 flags)
{
    if ((flags & PE32_LCMAP_LOWERCASE) && (flags & PE32_LCMAP_UPPERCASE)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_FLAGS);
        return 0;
    }
    return 1;
}

static pe32_u32 lc_map_char(pe32_u32 c, pe32_u32 flags)
{
    if (flags & PE32_LCMAP_LOWERCASE) {
        return ascii_lower(c);
    }
    if (flags & PE32_LCMAP_UPPERCASE) {
        return ascii_upper(c);
    }
    return c;
}

int PE32_WINAPI pe32_win32_lc_map_string_a(pe32_u32 locale, pe32_u32 flags,
                                          const char *source, int source_length,
                                          char *dest, int dest_length)
{
    pe32_u32 count, i;
    (void)locale;
    if (!source || source_length == 0 || source_length < -1 ||
        dest_length < 0 || (dest_length > 0 && !dest)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!lc_map_flags_valid(flags)) {
        return 0;
    }
    count = source_length == -1 ? string_length_a(source) + 1u
                                : (pe32_u32)source_length;
    if (dest_length == 0) {
        return (int)count;
    }
    if (count > (pe32_u32)dest_length) {
        pe32_win32_set_last_error(PE32_ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        dest[i] = (char)lc_map_char((pe32_u8)source[i], flags);
    }
    return (int)count;
}

int PE32_WINAPI pe32_win32_lc_map_string_w(pe32_u32 locale, pe32_u32 flags,
                                          const pe32_u16 *source,
                                          int source_length, pe32_u16 *dest,
                                          int dest_length)
{
    pe32_u32 count, i;
    (void)locale;
    if (!source || source_length == 0 || source_length < -1 ||
        dest_length < 0 || (dest_length > 0 && !dest)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!lc_map_flags_valid(flags)) {
        return 0;
    }
    count = source_length == -1 ? string_length_w(source) + 1u
                                : (pe32_u32)source_length;
    if (dest_length == 0) {
        return (int)count;
    }
    if (count > (pe32_u32)dest_length) {
        pe32_win32_set_last_error(PE32_ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        dest[i] = (pe32_u16)lc_map_char(source[i], flags);
    }
    return (int)count;
}

/* Resolves a CompareString length: -1 means NUL-terminated and excludes the
 * NUL. Returns 0 for an invalid length or NULL with a nonzero length. */
static int compare_length_a(const char *string, int length, pe32_u32 *count)
{
    if (length == -1) {
        if (!string) {
            return 0;
        }
        *count = string_length_a(string);
        return 1;
    }
    if (length < 0 || (length > 0 && !string)) {
        return 0;
    }
    *count = (pe32_u32)length;
    return 1;
}

static int compare_length_w(const pe32_u16 *string, int length,
                            pe32_u32 *count)
{
    if (length == -1) {
        if (!string) {
            return 0;
        }
        *count = string_length_w(string);
        return 1;
    }
    if (length < 0 || (length > 0 && !string)) {
        return 0;
    }
    *count = (pe32_u32)length;
    return 1;
}

/* Ordinal comparison, not Windows linguistic order. Results are
 * CSTR_LESS_THAN, CSTR_EQUAL or CSTR_GREATER_THAN. */
int PE32_WINAPI pe32_win32_compare_string_a(pe32_u32 locale, pe32_u32 flags,
                                           const char *string1, int length1,
                                           const char *string2, int length2)
{
    pe32_u32 count1, count2, count, i;
    pe32_u32 a, b;
    int ignore_case = (flags & PE32_NORM_IGNORECASE) != 0;
    (void)locale;
    if (!compare_length_a(string1, length1, &count1) ||
        !compare_length_a(string2, length2, &count2)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    count = count1 < count2 ? count1 : count2;
    for (i = 0; i < count; ++i) {
        a = (pe32_u8)string1[i];
        b = (pe32_u8)string2[i];
        if (ignore_case) {
            a = ascii_lower(a);
            b = ascii_lower(b);
        }
        if (a != b) {
            return a < b ? PE32_CSTR_LESS_THAN : PE32_CSTR_GREATER_THAN;
        }
    }
    if (count1 < count2) {
        return PE32_CSTR_LESS_THAN;
    }
    if (count1 > count2) {
        return PE32_CSTR_GREATER_THAN;
    }
    return PE32_CSTR_EQUAL;
}

int PE32_WINAPI pe32_win32_compare_string_w(pe32_u32 locale, pe32_u32 flags,
                                           const pe32_u16 *string1,
                                           int length1,
                                           const pe32_u16 *string2,
                                           int length2)
{
    pe32_u32 count1, count2, count, i;
    pe32_u32 a, b;
    int ignore_case = (flags & PE32_NORM_IGNORECASE) != 0;
    (void)locale;
    if (!compare_length_w(string1, length1, &count1) ||
        !compare_length_w(string2, length2, &count2)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    count = count1 < count2 ? count1 : count2;
    for (i = 0; i < count; ++i) {
        a = string1[i];
        b = string2[i];
        if (ignore_case) {
            a = ascii_lower(a);
            b = ascii_lower(b);
        }
        if (a != b) {
            return a < b ? PE32_CSTR_LESS_THAN : PE32_CSTR_GREATER_THAN;
        }
    }
    if (count1 < count2) {
        return PE32_CSTR_LESS_THAN;
    }
    if (count1 > count2) {
        return PE32_CSTR_GREATER_THAN;
    }
    return PE32_CSTR_EQUAL;
}

/* en-US values only. Returns 0 for unsupported LCTYPE values. */
static const char *locale_info_value(pe32_u32 type)
{
    switch (type) {
    case 0x0001u: /* LOCALE_ILANGUAGE */
        return "0409";
    case 0x0002u: /* LOCALE_SLANGUAGE */
        return "English (United States)";
    case 0x000eu: /* LOCALE_SDECIMAL */
        return ".";
    case 0x000fu: /* LOCALE_STHOUSAND */
        return ",";
    default:
        return 0;
    }
}

/* Validates lc_type and returns its value, or sets 1004 and returns 0. */
static const char *locale_info_lookup(pe32_u32 lc_type)
{
    const char *value;
    if (lc_type & ~(PE32_LCTYPE_FLAG_NOUSEROVERRIDE |
                    PE32_LCTYPE_FLAG_USE_CP_ACP | 0xffffu)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_FLAGS);
        return 0;
    }
    value = locale_info_value(lc_type & 0xffffu);
    if (!value) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_FLAGS);
    }
    return value;
}

int PE32_WINAPI pe32_win32_get_locale_info_a(pe32_u32 locale,
                                            pe32_u32 lc_type, char *buffer,
                                            int capacity)
{
    const char *value;
    pe32_u32 length, i;
    (void)locale;
    value = locale_info_lookup(lc_type);
    if (!value) {
        return 0;
    }
    length = string_length_a(value) + 1u;
    if (capacity < 0 || (capacity > 0 && !buffer)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (capacity == 0) {
        return (int)length;
    }
    if (length > (pe32_u32)capacity) {
        pe32_win32_set_last_error(PE32_ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < length; ++i) {
        buffer[i] = value[i];
    }
    return (int)length;
}

int PE32_WINAPI pe32_win32_get_locale_info_w(pe32_u32 locale,
                                            pe32_u32 lc_type, pe32_u16 *buffer,
                                            int capacity)
{
    const char *value;
    pe32_u32 length, i;
    (void)locale;
    value = locale_info_lookup(lc_type);
    if (!value) {
        return 0;
    }
    length = string_length_a(value) + 1u;
    if (capacity < 0 || (capacity > 0 && !buffer)) {
        pe32_win32_set_last_error(PE32_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (capacity == 0) {
        return (int)length;
    }
    if (length > (pe32_u32)capacity) {
        pe32_win32_set_last_error(PE32_ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (i = 0; i < length; ++i) {
        buffer[i] = (pe32_u16)(pe32_u8)value[i];
    }
    return (int)length;
}
